// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "simulation/Benchmark.h"
#include <functional>

namespace stellar
{

void
Benchmark::startBenchmark(Application& app)
{
    // TODO start timers
    std::function<bool()> load = [this]() bool {
        if (!this->mIsRunning)
        {
            return false;
        }
        generateLoadForBenchmark(this->mTxRate);

        return true;
    };
    scheduleLoad(app, load);
}

void
Benchmark::stopBenchmark()
{
    mIsRunning = false;
}

Metrics
Benchmark::getMetrics()
{
}

bool
Benchmark::generateLoadForBenchmark(uint32_t txRate)
{
    updateMinBalance(app);

    if (txRate == 0)
    {
        txRate = 1;
    }

    uint32_t txPerStep = (txRate * STEP_MSECS / 1000);

    if (txPerStep == 0)
    {
        txPerStep = 1;
    }

    uint32_t ledgerNum = app.getLedgerManager().getLedgerNum();
    vector<TxInfo> txs;

    for (uint32_t i = 0; i < txPerStep; ++i)
    {
        txs.push_back(createRandomTransaction(0.5, ledgerNum));
    }

    for (auto& tx : txs)
    {
        if (!tx.execute(app))
        {
            CLOG(ERROR, "Benchmark") << "Error while executing a transaction";
            return false;
        }
    }
    return true;
}

void
Benchmark::initializeBenchmark(Application& app)
{
    createAccounts(mNumberOfInitialAccounts);
}
}
