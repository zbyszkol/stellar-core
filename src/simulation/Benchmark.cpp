// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "simulation/Benchmark.h"
#include <vector>
#include <functional>
#include "util/Logging.h"

namespace stellar
{

void
Benchmark::startBenchmark()
{
    // TODO start timers
    std::function<bool()> load = [this] () -> bool {
        if (!this->mIsRunning)
        {
            return false;
        }
        generateLoadForBenchmark(mApp, this->mTxRate);

        return true;
    };
    // TODO
    // loadGenerator.scheduleLoad(app, load);
}

void
Benchmark::stopBenchmark()
{
    mIsRunning = false;
}

Benchmark::Metrics
Benchmark::getMetrics()
{
}

bool
Benchmark::generateLoadForBenchmark(Application& app, uint32_t txRate)
{
    updateMinBalance(app);

    if (txRate == 0)
    {
        txRate = 1;
    }

    uint32_t txPerStep = (txRate * LoadGenerator::STEP_MSECS / 1000);

    if (txPerStep == 0)
    {
        txPerStep = 1;
    }

    uint32_t ledgerNum = app.getLedgerManager().getLedgerNum();
    std::vector<LoadGenerator::TxInfo> txs;

    for (uint32_t i = 0; i < txPerStep; ++i)
    {
        txs.push_back(app.getLoadGenerator().createRandomTransaction(0.5, ledgerNum));
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
    for (uint32_t it = 0; it < mNumAccounts; ++it)
    {
        auto account = createAccount(it);
        account->createDirectly(app);
    }
}
}
