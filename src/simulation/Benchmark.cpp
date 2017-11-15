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
#include "database/Database.h"
#include "util/make_unique.h"
#include <memory>

namespace stellar
{

const char* Benchmark::LOGGER_ID = "LoadGen";

size_t Benchmark::MAXIMAL_NUMBER_OF_TXS_PER_LEDGER = 1000;

Benchmark::Benchmark(Hash const& networkID)
    : LoadGenerator(networkID), mIsRunning(false), mNumberOfInitialAccounts(10000), mTxRate(250)
{
}

std::shared_ptr<Benchmark::Metrics>
Benchmark::startBenchmark(Application& app)
{
    // TODO start timers
    mIsRunning = true;
    using namespace std;
    shared_ptr<Benchmark::Metrics> metrics{ initializeMetrics(app.getMetrics()) };

    std::function<bool()> load = [this, &app, metrics] () -> bool {
        if (!this->mIsRunning)
        {
            return false;
        }
        generateLoadForBenchmark(app, this->mTxRate, *metrics);

        return true;
    };
    metrics->benchmarkTimeContext.Reset();
    load();
    scheduleLoad(app, load);

    return metrics;
}

std::unique_ptr<Benchmark::Metrics>
Benchmark::initializeMetrics(medida::MetricsRegistry& registry)
{
    return make_unique<Benchmark::Metrics>(registry);
}

Benchmark::Metrics::Metrics(medida::MetricsRegistry& registry)
    : benchmarkTimer(registry.NewTimer({ "benchmark", "overall", "time" })),
      benchmarkTimeContext(benchmarkTimer.TimeScope()),
      txsCount(registry.NewCounter({ "benchmark", "txs", "count" }))
{
    benchmarkTimeContext.Stop();
}

std::shared_ptr<Benchmark::Metrics>
Benchmark::stopBenchmark(std::shared_ptr<Benchmark::Metrics> metrics)
{
    mIsRunning = false;
    metrics->benchmarkTimeContext.Stop();
    return metrics;
}

Benchmark::Metrics
Benchmark::getMetrics()
{
}

bool
Benchmark::generateLoadForBenchmark(Application& app, uint32_t txRate, Metrics& metrics)
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
    CLOG(TRACE, LOGGER_ID) << "Generating " << txPerStep << "transactions per step";

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
            CLOG(ERROR, LOGGER_ID) << "Error while executing a transaction";
            return false;
        }
    }

    metrics.txsCount.inc(txs.size());

    return true;
}

// std::vector<LoadGenerator::AccountInfoPtr>
void
Benchmark::createAccounts(Application& app, size_t n)
{
    auto accountsLeft = mNumberOfInitialAccounts;
    TxMetrics txm(app.getMetrics());
    LedgerManager& ledger = app.getLedgerManager();
    auto ledgerNum = ledger.getLedgerNum();
    StellarValue value(ledger.getLastClosedLedgerHeader().hash, ledgerNum,
                    emptyUpgradeSteps, 0);

    while (accountsLeft > 0)
    {
        auto ledgerNum = ledger.getLedgerNum();
        TxSetFramePtr txSet = std::make_shared<TxSetFrame>(
            ledger.getLastClosedLedgerHeader().hash);

        std::vector<TransactionFramePtr> txFrames;
        auto batchSize = std::min(accountsLeft, MAXIMAL_NUMBER_OF_TXS_PER_LEDGER);

        std::vector<LoadGenerator::AccountInfoPtr> newAccounts =
            LoadGenerator::createAccounts(batchSize, ledgerNum);

        for (AccountInfoPtr& account : newAccounts) {
            TxInfo tx = account->creationTransaction();
            // bool result = tx.execute(app);
            tx.toTransactionFrames(app, txFrames, txm);
            tx.recordExecution(app.getConfig().DESIRED_BASE_FEE);

        }

        for (TransactionFramePtr txFrame : txFrames) {
            txSet->add(txFrame);
        }

        StellarValue value(txSet->getContentsHash(), ledgerNum,
                           emptyUpgradeSteps, 0);
        auto closeData = LedgerCloseData{ledgerNum, txSet, value};
        app.getLedgerManager().valueExternalized(closeData);

        accountsLeft -= batchSize;
    }

    // return createAccounts;
}

void
Benchmark::createAccountsUsingTransactions(Application& app, size_t n)
{
    auto ledgerNum = app.getLedgerManager().getLedgerNum();
    std::vector<LoadGenerator::AccountInfoPtr> newAccounts = LoadGenerator::createAccounts(n, ledgerNum);
    for (LoadGenerator::AccountInfoPtr account : newAccounts) {
        LoadGenerator::TxInfo tx = account->creationTransaction();
        if (!tx.execute(app)) {
            CLOG(ERROR, LOGGER_ID) << "Error while executing CREATE_ACCOUNT transaction";
        }
    }
}

// std::vector<LoadGenerator::AccountInfoPtr>
void
Benchmark::createAccountsDirectly(Application& app, size_t n)
{
    soci::transaction sqlTx(app.getDatabase().getSession());

    auto ledger = app.getLedgerManager().getLedgerNum();
    std::vector<LoadGenerator::AccountInfoPtr> createdAccounts =
        LoadGenerator::createAccounts(mNumberOfInitialAccounts, ledger);

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

    sqlTx.commit();

    auto liveEntries = delta.getLiveEntries();
    live.insert(live.end(), liveEntries.begin(), liveEntries.end());
    app.getBucketManager().addBatch(app, ledger, live, {});

    StellarValue sv(app.getLedgerManager().getLastClosedLedgerHeader().hash, ledger,
                    emptyUpgradeSteps, 0);
    LedgerCloseData ledgerData = createData(app.getLedgerManager(), sv);
    app.getLedgerManager().closeLedger(ledgerData);

    mAccounts = createdAccounts;
}

void
Benchmark::setMaxTxSize(LedgerManager& ledger, uint32_t maxTxSetSize)
{
    StellarValue sv(ledger.getLastClosedLedgerHeader().hash, ledger.getLedgerNum(),
                    emptyUpgradeSteps, 0);
    {
        LedgerUpgrade up(LEDGER_UPGRADE_MAX_TX_SET_SIZE);
        up.newMaxTxSetSize() = maxTxSetSize;
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
Benchmark::prepareBenchmark(Application& app)
{
    CLOG(INFO, LOGGER_ID) << "Initializing benchmark...";

    initializeMetrics(app.getMetrics());

    app.newDB();

    setMaxTxSize(app.getLedgerManager(), MAXIMAL_NUMBER_OF_TXS_PER_LEDGER);

    auto ledger = app.getLedgerManager().getLedgerNum();

    createAccountsDirectly(app, mNumberOfInitialAccounts);
    // createAccounts(app, mNumberOfInitialAccounts);
    // createAccountsUsingTransactions(app, mNumberOfInitialAccounts);
}

void
Benchmark::initializeBenchmark(Application& app, uint32_t ledgerNum)
{
    // TODO create in-memory accounts
    mAccounts = LoadGenerator::createAccounts(mNumberOfInitialAccounts,  ledgerNum);
    mRandomIterator = shuffleAccounts(mAccounts);
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
