#pragma once

// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "crypto/SecretKey.h"
#include "simulation/LoadGenerator.h"
#include "main/Application.h"
#include "test/TxTests.h"
#include "xdr/Stellar-types.h"
#include <vector>
#include "medida/metrics_registry.h"
#include "medida/counter.h"
#include <memory>

namespace medida
{
class MetricsRegistry;
class Meter;
class Counter;
class Timer;
}

namespace stellar
{

class Benchmark : private LoadGenerator
{
public:
    static size_t MAXIMAL_NUMBER_OF_TXS_PER_LEDGER;

    struct Metrics
    {
        Metrics(medida::MetricsRegistry& registry);

        medida::Timer& benchmarkTimer;
        medida::TimerContext benchmarkTimeContext;
        medida::Counter& txsCount;
    };

    Benchmark(Hash const& networkID);
    void prepareBenchmark(Application& app);
    void initializeBenchmark(Application& app, uint32_t ledgerNum);
    std::shared_ptr<Metrics> startBenchmark(Application& app);
    std::shared_ptr<Metrics> stopBenchmark(std::shared_ptr<Metrics> metrics);
    virtual LoadGenerator::AccountInfoPtr pickRandomAccount(AccountInfoPtr tryToAvoid, uint32_t ledgerNum) override;
    // std::vector<LoadGenerator::AccountInfoPtr>
    void createAccountsDirectly(Application& app, size_t n);
    void createAccounts(Application& app, size_t n);
    void createAccountsUsingTransactions(Application& app, size_t n);

private:
    void setMaxTxSize(LedgerManager& ledger, uint32_t maxTxSetSize);
    bool generateLoadForBenchmark(Application& app, uint32_t txRate, Metrics& metrics);
    std::vector<AccountInfoPtr>::iterator shuffleAccounts(std::vector<LoadGenerator::AccountInfoPtr>& accounts);
    LedgerCloseData createData(LedgerManager& ledger, StellarValue& value);
    std::unique_ptr<Benchmark::Metrics> initializeMetrics(medida::MetricsRegistry& registry);
    bool mIsRunning;
    size_t mNumberOfInitialAccounts;
    uint32_t mTxRate;
    std::vector<AccountInfoPtr>::iterator mRandomIterator;

    static const char* LOGGER_ID;
};

}
