// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "simulation/Benchmark.h"

#include <algorithm>
#include <vector>
#include <functional>
#include <random>
#include "util/Logging.h"
#include "bucket/BucketManager.h"
#include "ledger/LedgerDelta.h"

namespace stellar
{

Benchmark::Benchmark(Hash const& networkID)
    : LoadGenerator(networkID), mIsRunning(false), mNumberOfInitialAccounts(5000), mTxRate(10)
{
}

void
Benchmark::startBenchmark(Application& app)
{
    // TODO start timers
    mIsRunning = true;
    std::function<bool()> load = [this, &app] () -> bool {
        if (!this->mIsRunning)
        {
            return false;
        }
        generateLoadForBenchmark(app, this->mTxRate);

        return true;
    };
    // TODO
    scheduleLoad(app, load);
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
    CLOG(INFO, "Benchmark") << "Initializing benchmark...";
    app.newDB();
    // app.getLedgerManager().startNewLedger();

    auto ledger = app.getLedgerManager().getLedgerNum();
    std::vector<LoadGenerator::AccountInfoPtr> createdAccounts = createAccounts(mNumberOfInitialAccounts, ledger);
    std::vector<LedgerEntry> live;
    // auro header = app.getLedgerManager().getLeadgerHeader();
    LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(), app.getDatabase());
    int64_t balanceDiff = 0;
    std::transform(createdAccounts.begin(), createdAccounts.end(), std::back_inserter(live),
                   [&app, &balanceDiff, &delta] (LoadGenerator::AccountInfoPtr const& account)
                   {
                       AccountFrame aFrame = account->createDirectly(app);
                       // delta.addEntry(aFrame);
                       balanceDiff -= aFrame.getBalance();
                       return aFrame.mEntry;
                   });

    // std::for_each(createdAccounts.begin(), createdAccounts.end(),
    //               [&app, &balanceDiff, &delta](LoadGenerator::AccountInfoPtr const& account) {
    //                   AccountFrame aFrame = account->createDirectly(app);
    //                   delta.addEntry(aFrame);
    //                   balanceDiff -= aFrame.getBalance();
    //               });

    // TODO stellar-core is throwing an exception with invalid totalcoinsinvariant
    SecretKey skey = SecretKey::fromSeed(app.getNetworkID());
    AccountFrame::pointer masterAccount = AccountFrame::loadAccount(skey.getPublicKey(), app.getDatabase());
    // AccountFrame masterAccount(skey.getPublicKey());
    // masterAccount.loadAcc

    masterAccount->addBalance(balanceDiff); // mNumberOfInitialAccounts * LoadGenerator::LOADGEN_ACCOUNT_BALANCE;
    masterAccount->touch(ledger);
    masterAccount->storeChange(delta, app.getDatabase());

    // delta.commit();

    live.push_back(masterAccount->mEntry);
    app.getBucketManager().addBatch(app, ledger, live, {});
    // app.getLedgerManager().ledgerClosed(delta);
    TxSetFramePtr txSet = std::make_shared<TxSetFrame>(
        app.getLedgerManager().getLastClosedLedgerHeader().hash);
    StellarValue sv(txSet->getContentsHash(),
                    VirtualClock::to_time_t(app.getClock().now()),
                    emptyUpgradeSteps, 0);
    LedgerCloseData ledgerData(app.getLedgerManager().getLedgerNum(),
                               txSet, sv);
    app.getLedgerManager().closeLedger(ledgerData);

    auto rng = std::default_random_engine {};
    std::shuffle(mAccounts.begin(), mAccounts.end(), rng);
    mRandomIterator = mAccounts.begin();
}

LoadGenerator::AccountInfoPtr
Benchmark::pickRandomAccount(AccountInfoPtr tryToAvoid, uint32_t ledgerNum)
{
    if (mRandomIterator == mAccounts.end()) {
        mRandomIterator = mAccounts.begin();
    }
    auto result = *mRandomIterator;
    mRandomIterator++;
    return result;
}
}
