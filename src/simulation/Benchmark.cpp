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
    : LoadGenerator(networkID), mIsRunning(false), mNumberOfInitialAccounts(5000), mTxRate(250)
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
    CLOG(TRACE, "Benchmark") << "Going to generate " << txPerStep << "transactions per step";

    uint32_t ledgerNum = app.getLedgerManager().getLedgerNum();
    std::vector<LoadGenerator::TxInfo> txs;

    for (uint32_t it = 0; it < txPerStep; ++it)
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

std::vector<LoadGenerator::AccountInfoPtr>
Benchmark::createAccountsDirectly(Application& app, size_t n, uint32_t ledgerNum)
{
    auto ledger = app.getLedgerManager().getLedgerNum();
    std::vector<LoadGenerator::AccountInfoPtr> createdAccounts = createAccounts(mNumberOfInitialAccounts, ledger);

    int64_t balanceDiff = 0;
    std::vector<LedgerEntry> live;
    std::transform(createdAccounts.begin(), createdAccounts.end(), std::back_inserter(live),
                   [&app, &balanceDiff] (LoadGenerator::AccountInfoPtr const& account)
                   {
                       AccountFrame aFrame = account->createDirectly(app);
                       balanceDiff += aFrame.getBalance();
                       return aFrame.mEntry;
                   });

    SecretKey skey = SecretKey::fromSeed(app.getNetworkID());
    AccountFrame::pointer masterAccount = AccountFrame::loadAccount(skey.getPublicKey(), app.getDatabase());

    LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(), app.getDatabase());
    masterAccount->addBalance(-balanceDiff);
    masterAccount->touch(ledger);
    masterAccount->storeChange(delta, app.getDatabase());

    live.push_back(masterAccount->mEntry);
    app.getBucketManager().addBatch(app, ledger, live, {});

    StellarValue sv(app.getLedgerManager().getLastClosedLedgerHeader().hash, ledger,
                    emptyUpgradeSteps, 0);
    LedgerCloseData ledgerData = createData(app.getLedgerManager(), sv);
    app.getLedgerManager().closeLedger(ledgerData);

    return createdAccounts;
}

void
Benchmark::setMaxTxSize(LedgerManager& ledger)
{
    StellarValue sv(ledger.getLastClosedLedgerHeader().hash, ledger.getLedgerNum(),
                    emptyUpgradeSteps, 0);
    {
        LedgerUpgrade up(LEDGER_UPGRADE_MAX_TX_SET_SIZE);
        up.newMaxTxSetSize() = 1300;
        Value v(xdr::xdr_to_opaque(up));
        sv.upgrades.emplace_back(v.begin(), v.end());
    }
    LedgerCloseData ledgerData = createData(ledger, sv);
    ledger.closeLedger(ledgerData);
}

LedgerCloseData
Benchmark::createData(LedgerManager& ledger, StellarValue& value)
{
    auto ledgerNum = ledger.getLedgerNum();
    TxSetFramePtr txSet = std::make_shared<TxSetFrame>(
        ledger.getLastClosedLedgerHeader().hash);
    value.txSetHash = txSet->getContentsHash();
    return LedgerCloseData{ledgerNum, txSet, value};
}

void
Benchmark::initializeBenchmark(Application& app)
{
    CLOG(INFO, "Benchmark") << "Initializing benchmark...";
    app.newDB();

    auto ledger = app.getLedgerManager().getLedgerNum();

    mAccounts = createAccountsDirectly(app, mNumberOfInitialAccounts, ledger);
    mRandomIterator = shuffleAccounts(mAccounts);

    setMaxTxSize(app.getLedgerManager());
}

std::vector<LoadGenerator::AccountInfoPtr>::iterator
Benchmark::shuffleAccounts(std::vector<LoadGenerator::AccountInfoPtr>& accounts)
{
    auto rng = std::default_random_engine{};
    std::shuffle(mAccounts.begin(), mAccounts.end(), rng);
    return mAccounts.begin();
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
