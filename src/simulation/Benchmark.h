#pragma once

// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "main/Application.h"
#include "medida/counter.h"
#include "medida/metrics_registry.h"
#include "simulation/LoadGenerator.h"
#include <chrono>
#include <memory>
#include <vector>

namespace medida
{
class MetricsRegistry;
class Meter;
class Counter;
class Timer;
}

namespace stellar
{

class Benchmark : public LoadGenerator
{
  private:
  public:
    static size_t MAXIMAL_NUMBER_OF_TXS_PER_LEDGER;
    static const char* LOGGER_ID;

    struct Metrics
    {
        medida::Timer& benchmarkTimer;
        medida::Counter& txsCount;
        std::chrono::nanoseconds timeSpent;

      private:
        Metrics(medida::MetricsRegistry& registry);
        friend class Benchmark;
    };

    Benchmark(Hash const& networkID);
    Benchmark(Hash const& networkID, size_t numberOfInitialAccounts,
              uint32_t txRate);
    virtual ~Benchmark();
    void prepareBenchmark(Application& app);
    Benchmark& initializeBenchmark(Application& app, uint32_t ledgerNum);
    void startBenchmark(Application& app);
    Metrics stopBenchmark();
    bool isRunning();
    void setNumberOfInitialAccounts(size_t numberOfInitialAccounts);
    void setTxRate(uint32_t txRate);

  private:
    void populateAccounts(Application& app, size_t n);
    void setMaxTxSize(LedgerManager& ledger, uint32_t maxTxSetSize);
    bool generateLoadForBenchmark(Application& app, uint32_t txRate,
                                  Metrics& metrics);
    std::vector<AccountInfoPtr>::iterator
    shuffleAccounts(std::vector<LoadGenerator::AccountInfoPtr>& accounts);
    LedgerCloseData createData(LedgerManager& ledger, StellarValue& value);
    std::unique_ptr<Benchmark::Metrics>
    initializeMetrics(medida::MetricsRegistry& registry);
    virtual LoadGenerator::AccountInfoPtr
    pickRandomAccount(AccountInfoPtr tryToAvoid, uint32_t ledgerNum) override;
    void createAccountsDirectly(Application& app, size_t n);
    void createAccountsUsingLedgerManager(Application& app, size_t n);
    void createAccountsUsingTransactions(Application& app, size_t n);

    bool mIsRunning;
    size_t mNumberOfInitialAccounts;
    uint32_t mTxRate;
    std::vector<AccountInfoPtr>::iterator mRandomIterator;
    std::unique_ptr<Benchmark::Metrics> mMetrics;
    std::unique_ptr<medida::TimerContext> mBenchmarkTimeContext;
};
}
