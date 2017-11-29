// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "simulation/Benchmark.h"

#include "bucket/BucketManager.h"
#include "database/Database.h"
#include "herder/Herder.h"
#include "ledger/LedgerDelta.h"
#include "util/Logging.h"
#include "util/make_unique.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <random>
#include <vector>

namespace stellar
{

const char* Benchmark::LOGGER_ID = "Benchmark";

size_t Benchmark::MAXIMAL_NUMBER_OF_TXS_PER_LEDGER = 1000;

Benchmark::Benchmark(Hash const& networkID)
    : Benchmark(networkID, 1000, MAXIMAL_NUMBER_OF_TXS_PER_LEDGER)
{
}

Benchmark::Benchmark(Hash const& networkID, size_t numberOfInitialAccounts,
                     uint32_t txRate)
    : LoadGenerator(networkID)
    , mIsRunning(false)
    , mNumberOfInitialAccounts(numberOfInitialAccounts)
    , mTxRate(txRate)
{
}

Benchmark::~Benchmark()
{
    if (mIsRunning)
    {
        stopBenchmark();
    }
}

void
Benchmark::startBenchmark(Application& app)
{
    if (mIsRunning)
    {
        throw std::runtime_error{"Benchmark already started"};
    }
    mIsRunning = true;
    mMetrics = initializeMetrics(app.getMetrics());
    using namespace std;
    size_t txPerStep = (mTxRate * STEP_MSECS / 1000);
    txPerStep = max(txPerStep, size_t(1));
    function<bool()> load = [this, &app, txPerStep]() {
        if (!this->mIsRunning)
        {
            return false;
        }

        generateLoadForBenchmark(app, txPerStep, *mMetrics);

        return true;
    };
    mBenchmarkTimeContext =
        make_unique<medida::TimerContext>(mMetrics->benchmarkTimer.TimeScope());
    load();
    scheduleLoad(app, load, LoadGenerator::STEP_MSECS);
}

std::unique_ptr<Benchmark::Metrics>
Benchmark::initializeMetrics(medida::MetricsRegistry& registry)
{
    return make_unique<Benchmark::Metrics>(Benchmark::Metrics(registry));
}

Benchmark::Metrics::Metrics(medida::MetricsRegistry& registry)
    : benchmarkTimer(registry.NewTimer({"benchmark", "overall", "time"}))
    , txsCount(registry.NewCounter({"benchmark", "txs", "count"}))
{
}

Benchmark::Metrics
Benchmark::stopBenchmark()
{
    CLOG(INFO, LOGGER_ID) << "Stopping benchmark";
    if (!mIsRunning)
    {
        throw std::runtime_error{"Benchmark is already stopped"};
    }
    mMetrics->timeSpent = mBenchmarkTimeContext->Stop();
    mIsRunning = false;
    mBenchmarkTimeContext.reset();
    auto result = *mMetrics;
    mMetrics.reset();
    CLOG(INFO, LOGGER_ID) << "Benchmark stopped";
    return result;
}

bool
Benchmark::isRunning()
{
    return mIsRunning;
}

bool
Benchmark::generateLoadForBenchmark(Application& app, uint32_t txRate,
                                    Metrics& metrics)
{
    updateMinBalance(app);

    if (txRate == 0)
    {
        txRate = 1;
    }

    CLOG(TRACE, LOGGER_ID) << "Generating " << txRate
                           << " transaction(s) per step";

    uint32_t ledgerNum = app.getLedgerManager().getLedgerNum();
    for (uint32_t it = 0; it < txRate; ++it)
    {
        auto tx = createRandomTransaction(0.5, ledgerNum);
        if (!tx.execute(app))
        {
            CLOG(ERROR, LOGGER_ID)
                << "Error while executing a transaction: transaction was rejected";
            return false;
        }
        metrics.txsCount.inc();
    }

    CLOG(TRACE, LOGGER_ID) << txRate
                           << " transaction(s) generated in a single step";

    return true;
}

void
Benchmark::createAccountsUsingLedgerManager(Application& app, size_t size)
{
    TxMetrics txm(app.getMetrics());
    LedgerManager& ledger = app.getLedgerManager();
    auto ledgerNum = ledger.getLedgerNum();
    TxSetFramePtr txSet = std::make_shared<TxSetFrame>(
        ledger.getLastClosedLedgerHeader().hash);

    std::vector<TransactionFramePtr> txFrames;
    std::vector<LoadGenerator::AccountInfoPtr> newAccounts =
        LoadGenerator::createAccounts(size, ledgerNum);

    for (AccountInfoPtr& account : newAccounts)
    {
        TxInfo tx = account->creationTransaction();
        tx.toTransactionFrames(app, txFrames, txm);
        tx.recordExecution(app.getConfig().DESIRED_BASE_FEE);
    }

    for (TransactionFramePtr txFrame : txFrames)
    {
        txSet->add(txFrame);
    }

    StellarValue value(txSet->getContentsHash(), ledgerNum,
                       emptyUpgradeSteps, 0);
    auto closeData = LedgerCloseData{ledgerNum, txSet, value};
    app.getLedgerManager().valueExternalized(closeData);
}

void
Benchmark::createAccountsUsingTransactions(Application& app, size_t n)
{
    auto ledgerNum = app.getLedgerManager().getLedgerNum();
    std::vector<LoadGenerator::AccountInfoPtr> newAccounts =
        LoadGenerator::createAccounts(n, ledgerNum);
    for (LoadGenerator::AccountInfoPtr account : newAccounts)
    {
        LoadGenerator::TxInfo tx = account->creationTransaction();
        if (!tx.execute(app))
        {
            CLOG(ERROR, LOGGER_ID)
                << "Error while executing CREATE_ACCOUNT transaction";
        }
    }
}

void
Benchmark::createAccountsDirectly(Application& app, size_t n)
{
    soci::transaction sqlTx(app.getDatabase().getSession());

    auto ledger = app.getLedgerManager().getLedgerNum();
    auto createdAccounts = LoadGenerator::createAccounts(n, ledger);

    int64_t balanceDiff = 0;
    std::vector<LedgerEntry> live;
    std::transform(
        createdAccounts.begin(), createdAccounts.end(),
        std::back_inserter(live),
        [&app, &balanceDiff](LoadGenerator::AccountInfoPtr const& account) {
            AccountFrame aFrame = account->createDirectly(app);
            balanceDiff += aFrame.getBalance();
            return aFrame.mEntry;
        });

    SecretKey skey = SecretKey::fromSeed(app.getNetworkID());
    AccountFrame::pointer masterAccount =
        AccountFrame::loadAccount(skey.getPublicKey(), app.getDatabase());
    LedgerDelta delta(app.getLedgerManager().getCurrentLedgerHeader(),
                      app.getDatabase());
    masterAccount->addBalance(-balanceDiff);
    masterAccount->touch(ledger);
    masterAccount->storeChange(delta, app.getDatabase());

    sqlTx.commit();

    auto liveEntries = delta.getLiveEntries();
    live.insert(live.end(), liveEntries.begin(), liveEntries.end());
    app.getBucketManager().addBatch(app, ledger, live, {});

    StellarValue sv(app.getLedgerManager().getLastClosedLedgerHeader().hash,
                    ledger, emptyUpgradeSteps, 0);
    LedgerCloseData ledgerData = createData(app.getLedgerManager(), sv);
    app.getLedgerManager().closeLedger(ledgerData);
}

void
Benchmark::populateAccounts(Application& app, size_t size)
{
    for (size_t accountsLeft = size, batchSize = size; accountsLeft > 0; accountsLeft -= batchSize)
    {
        batchSize = std::min(accountsLeft, MAXIMAL_NUMBER_OF_TXS_PER_LEDGER);
        createAccountsDirectly(app, batchSize);
    }
    app.getHerder().triggerNextLedger(app.getLedgerManager().getLedgerNum());
}

void
Benchmark::setMaxTxSize(LedgerManager& ledger, uint32_t maxTxSetSize)
{
    StellarValue sv(ledger.getLastClosedLedgerHeader().hash,
                    ledger.getLedgerNum(), emptyUpgradeSteps, 0);
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
    TxSetFramePtr txSet =
        std::make_shared<TxSetFrame>(ledger.getLastClosedLedgerHeader().hash);
    value.txSetHash = txSet->getContentsHash();
    return LedgerCloseData{ledgerNum, txSet, value};
}

void
Benchmark::prepareBenchmark(Application& app)
{
    CLOG(INFO, LOGGER_ID) << "Preparing data for benchmark";

    initializeMetrics(app.getMetrics());
    setMaxTxSize(app.getLedgerManager(), MAXIMAL_NUMBER_OF_TXS_PER_LEDGER);
    populateAccounts(app, mNumberOfInitialAccounts);

    app.getHistoryManager().queueCurrentHistory();
    app.getHerder().triggerNextLedger(app.getLedgerManager().getLedgerNum());

    mAccounts.clear();

    CLOG(INFO, LOGGER_ID) << "Data for benchmark prepared";
}

Benchmark&
Benchmark::initializeBenchmark(Application& app)
{
    CLOG(INFO, LOGGER_ID) << "Initializing benchmark";

    mAccounts = LoadGenerator::createAccounts(mNumberOfInitialAccounts, app.getLedgerManager().getLedgerNum());
    loadAccounts(app, mAccounts);
    mRandomIterator = shuffleAccounts(mAccounts);
    setMaxTxSize(app.getLedgerManager(), MAXIMAL_NUMBER_OF_TXS_PER_LEDGER);
    app.getHerder().triggerNextLedger(app.getLedgerManager().getLedgerNum());

    CLOG(INFO, LOGGER_ID) << "Benchmark initialized";
    return *this;
}

std::vector<LoadGenerator::AccountInfoPtr>::iterator
Benchmark::shuffleAccounts(std::vector<LoadGenerator::AccountInfoPtr>& accounts)
{
    auto rng = std::default_random_engine{0};
    std::shuffle(mAccounts.begin(), mAccounts.end(), rng);
    return mAccounts.begin();
}

LoadGenerator::AccountInfoPtr
Benchmark::pickRandomAccount(AccountInfoPtr tryToAvoid, uint32_t ledgerNum)
{
    if (mRandomIterator == mAccounts.end())
    {
        mRandomIterator = mAccounts.begin();
    }
    auto result = *mRandomIterator;
    mRandomIterator++;
    return result;
}

void
Benchmark::setNumberOfInitialAccounts(size_t numberOfInitialAccounts)
{
    mNumberOfInitialAccounts = numberOfInitialAccounts;
}

void
Benchmark::setTxRate(uint32_t txRate)
{
    mTxRate = txRate;
}
}
