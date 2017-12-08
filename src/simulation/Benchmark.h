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
#include "xdr/Stellar-types.h"
#include <chrono>
#include <memory>
#include <vector>

namespace stellar
{

static const size_t MAXIMAL_NUMBER_OF_ACCOUNTS_IN_BATCH = 10000;

class TxSampler;

class Benchmark
{
    static const uint32_t STEP_MSECS;

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

    Benchmark(medida::MetricsRegistry& registry, uint32_t txRate,
              std::unique_ptr<TxSampler> sampler);
    ~Benchmark();
    void startBenchmark(Application& app);
    Metrics stopBenchmark();
    void setTxRate(uint32_t txRate);

  private:
    bool generateLoadForBenchmark(Application& app);
    void scheduleLoad(Application& app, std::chrono::milliseconds stepTime);

    bool mIsRunning;
    uint32_t mTxRate;
    Benchmark::Metrics mMetrics;
    std::unique_ptr<medida::TimerContext> mBenchmarkTimeContext;
    std::unique_ptr<VirtualTimer> mLoadTimer;
    std::unique_ptr<TxSampler> mSampler;
};

class TxSampler : private LoadGenerator
{
  public:
    class Tx;

    TxSampler(Hash const& networkID);
    void initialize(Application& app);
    void loadAccounts(Application& app);
    std::unique_ptr<Tx> createTransaction(size_t size);
    std::vector<LoadGenerator::AccountInfoPtr>
    createAccounts(size_t batchSize, uint32_t ledgerNum);
    std::vector<LoadGenerator::AccountInfoPtr> const& getAccounts();

  private:
    virtual LoadGenerator::AccountInfoPtr
    pickRandomAccount(LoadGenerator::AccountInfoPtr tryToAvoid,
                      uint32_t ledgerNum) override;
    std::vector<LoadGenerator::AccountInfoPtr>::iterator
    shuffleAccounts(std::vector<LoadGenerator::AccountInfoPtr>& accounts);

    std::vector<LoadGenerator::AccountInfoPtr>::iterator mRandomIterator;
};

class TxSampler::Tx
{
  public:
    bool execute(Application& app);

  private:
    std::vector<LoadGenerator::TxInfo> mTxs;
    friend class TxSampler;
};
}
