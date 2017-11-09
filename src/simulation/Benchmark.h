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
    Benchmark(Hash const& networkID);
    struct Metrics
    {
    };

    void initializeBenchmark(Application& app);
    void startBenchmark(Application& app);
    void stopBenchmark();
    Metrics getMetrics();
    virtual LoadGenerator::AccountInfoPtr pickRandomAccount(AccountInfoPtr tryToAvoid, uint32_t ledgerNum) override;
    std::vector<LoadGenerator::AccountInfoPtr> createAccountsDirectly(Application& app, size_t n, uint32_t ledgerNum);

private:
    bool generateLoadForBenchmark(Application& app, uint32_t txRate);
    std::vector<AccountInfoPtr>::iterator shuffleAccounts(std::vector<LoadGenerator::AccountInfoPtr>& accounts);
    void setMaxTxSize(LedgerManager& ledger);
    LedgerCloseData createData(LedgerManager& ledger, StellarValue& value);
    bool mIsRunning;
    size_t mNumberOfInitialAccounts;
    uint32_t mTxRate;
    std::vector<AccountInfoPtr>::iterator mRandomIterator;
};

}
