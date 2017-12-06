#pragma once

// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "main/Application.h"
#include "medida/counter.h"
#include "medida/metrics_registry.h"
#include "medida/reporting/json_reporter.h"
#include "medida/timer.h"
#include "simulation/LoadGenerator.h"
#include "util/Timer.h"
#include <chrono>
#include <memory>
#include <vector>

namespace stellar
{

class TxSampler;

class Benchmark
{
  public:
    class BenchmarkBuilder;

    struct Metrics
    {
        medida::Timer& mBenchmarkTimer;
        medida::Counter& mTxsCount;

      private:
        Metrics(medida::MetricsRegistry& registry);
        friend class Benchmark;
    };

    ~Benchmark();
    void startBenchmark(Application& app);
    Metrics stopBenchmark();

  protected:
    Benchmark(medida::MetricsRegistry& registry, uint32_t txRate,
              std::unique_ptr<TxSampler> sampler);

  private:
    bool generateLoadForBenchmark(Application& app);
    void scheduleLoad(Application& app,
                      std::chrono::milliseconds stepTime);
    Benchmark::Metrics initializeMetrics(medida::MetricsRegistry& registry);

    bool mIsRunning;
    uint32_t mTxRate;
    Benchmark::Metrics mMetrics;
    std::unique_ptr<medida::TimerContext> mBenchmarkTimeContext;
    std::unique_ptr<VirtualTimer> mLoadTimer;
    std::unique_ptr<TxSampler> mSampler;

    static const size_t MAXIMAL_NUMBER_OF_ACCOUNTS_IN_BATCH;
};

class Benchmark::BenchmarkBuilder
{
  public:
    BenchmarkBuilder(Hash const& networkID);
    BenchmarkBuilder& setNumberOfInitialAccounts(uint32_t accounts);
    BenchmarkBuilder& setTxRate(uint32_t txRate);
    BenchmarkBuilder& initializeBenchmark();
    BenchmarkBuilder& populateBenchmarkData();
    std::unique_ptr<Benchmark> createBenchmark(Application& app) const;
    std::unique_ptr<TxSampler> createSampler(Application& app);

  private:
    void prepareBenchmark(Application& app,
                          TxSampler& sampler) const;
    void populateAccounts(Application& app, size_t n,
                          TxSampler& sampler) const;
    void createAccountsDirectly(
        Application& app,
        std::vector<LoadGenerator::AccountInfoPtr>& accounts) const;

    bool mInitialize;
    bool mPopulate;
    uint32_t mTxRate;
    uint32_t mNumberOfAccounts;
    Hash mNetworkID;
};

class TxSampler : private LoadGenerator
{
  public:
    class Tx;

    TxSampler(Hash const& networkID);
    std::unique_ptr<Tx> createTransaction(size_t size);

  private:
    void initialize(Application& app, size_t numberOfAccounts);
    std::vector<LoadGenerator::AccountInfoPtr> createAccounts(size_t batchSize);
    virtual LoadGenerator::AccountInfoPtr
    pickRandomAccount(LoadGenerator::AccountInfoPtr tryToAvoid,
                      uint32_t ledgerNum) override;
    std::vector<LoadGenerator::AccountInfoPtr>::iterator
    shuffleAccounts(std::vector<LoadGenerator::AccountInfoPtr>& accounts);

    std::vector<LoadGenerator::AccountInfoPtr>::iterator mRandomIterator;

    friend class Benchmark::BenchmarkBuilder;
};

class TxSampler::Tx
{
public:
    bool execute(Application& app);
private:
    std::vector<LoadGenerator::TxInfo> mTxs;

    friend class TxSampler;
};

class BenchmarkExecutor
{
  public:
    void executeBenchmark(Application& app,
                          Benchmark::BenchmarkBuilder& benchmarkBuilder,
                          std::chrono::seconds testDuration,
                          std::function<void(Benchmark::Metrics)> stopCallback);

  private:
    std::unique_ptr<VirtualTimer> mLoadTimer;
};

class BenchmarkReporter
{
public:
    template <typename Stream>
    void
    reportBenchmark(Benchmark::Metrics const& metrics,
                    medida::MetricsRegistry& metricsRegistry, Stream& str)
    {
        struct ReportProcessor : medida::MetricProcessor
        {
            virtual void
            Process(medida::Timer& timer) override
            {
                count = timer.count();
            }

            std::uint64_t count;
        };
        auto externalizedTxs =
            metricsRegistry.GetAllMetrics()[{"ledger", "transaction", "apply"}];
        ReportProcessor processor;
        externalizedTxs->Process(processor);
        auto txsExternalized = processor.count;

        using std::endl;
        str << endl
            << "Benchmark metrics:" << endl
            << "  time spent: " << metrics.mBenchmarkTimer.sum()
            << " milliseconds" << endl
            << "  txs submitted: " << metrics.mTxsCount.count() << endl
            << "  txs externalized: " << txsExternalized << endl;

        medida::reporting::JsonReporter jr(metricsRegistry);
        str << jr.Report() << endl;
    }
};
}
