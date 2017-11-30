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
#include <chrono>
#include <functional>
#include <memory>
#include <random>
#include <vector>

namespace stellar
{

const char* Benchmark::LOGGER_ID = "Benchmark";

size_t Benchmark::MAXIMAL_NUMBER_OF_TXS_PER_LEDGER = 1000;

Benchmark::Benchmark(uint32_t txRate, std::unique_ptr<TxSampler> sampler)
    : mTxRate(txRate), mSampler(std::move(sampler))
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
    size_t txPerStep = (mTxRate * LoadGenerator::STEP_MSECS / 1000);
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
    scheduleLoad(app, load, std::chrono::milliseconds{LoadGenerator::STEP_MSECS});
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
Benchmark::generateLoadForBenchmark(Application& app, uint32_t txRate,
                                    Metrics& metrics)
{
    CLOG(TRACE, LOGGER_ID) << "Generating " << txRate
                           << " transaction(s) per step";

    for (uint32_t it = 0; it < txRate; ++it)
    {
        uint32_t ledgerNum = app.getLedgerManager().getLedgerNum();
        auto tx = mSampler->createTransaction();
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
Benchmark::BenchmarkBuilder::createAccountsDirectly(Application& app,  std::vector<LoadGenerator::AccountInfoPtr>& createdAccounts) const
{
    soci::transaction sqlTx(app.getDatabase().getSession());

    auto ledger = app.getLedgerManager().getLedgerNum();

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
Benchmark::BenchmarkBuilder::populateAccounts(Application& app, size_t size, ShuffleLoadGenerator& sampler) const
{
    for (size_t accountsLeft = size, batchSize = size; accountsLeft > 0; accountsLeft -= batchSize)
    {
        batchSize = std::min(accountsLeft, MAXIMAL_NUMBER_OF_TXS_PER_LEDGER);
        auto newAccounts = sampler.createAccounts(batchSize);
        createAccountsDirectly(app, newAccounts);
    }
}

void
Benchmark::BenchmarkBuilder::setMaxTxSize(LedgerManager& ledger, uint32_t maxTxSetSize) const
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
Benchmark::BenchmarkBuilder::createData(LedgerManager& ledger, StellarValue& value) const
{
    auto ledgerNum = ledger.getLedgerNum();
    TxSetFramePtr txSet =
        std::make_shared<TxSetFrame>(ledger.getLastClosedLedgerHeader().hash);
    value.txSetHash = txSet->getContentsHash();
    return LedgerCloseData{ledgerNum, txSet, value};
}

void
Benchmark::BenchmarkBuilder::prepareBenchmark(Application& app, ShuffleLoadGenerator& sampler) const
{
    populateAccounts(app, mAccounts, sampler);
}

ShuffleLoadGenerator::ShuffleLoadGenerator(Hash const& networkID)
    : LoadGenerator(networkID)
{
}

ShuffleLoadGenerator::~ShuffleLoadGenerator()
{
}

LoadGenerator::TxInfo
ShuffleLoadGenerator::createTransaction()
{
    return LoadGenerator::createRandomTransaction(0.5);
}

std::vector<LoadGenerator::AccountInfoPtr>
ShuffleLoadGenerator::createAccounts(size_t batchSize)
{
    return LoadGenerator::createAccounts(batchSize);
}

void
ShuffleLoadGenerator::initialize(Application& app, size_t numberOfAccounts)
{
    CLOG(INFO, Benchmark::LOGGER_ID) << "Initializing benchmark";

    if (mAccounts.empty())
    {
        mAccounts = LoadGenerator::createAccounts(numberOfAccounts);
        loadAccounts(app, mAccounts);
    }
    mRandomIterator = shuffleAccounts(mAccounts);

    CLOG(INFO, Benchmark::LOGGER_ID) << "Benchmark initialized";
}

std::vector<LoadGenerator::AccountInfoPtr>::iterator
ShuffleLoadGenerator::shuffleAccounts(std::vector<LoadGenerator::AccountInfoPtr>& accounts)
{
    auto rng = std::default_random_engine{0};
    std::shuffle(mAccounts.begin(), mAccounts.end(), rng);
    return mAccounts.begin();
}

LoadGenerator::AccountInfoPtr
ShuffleLoadGenerator::pickRandomAccount(AccountInfoPtr tryToAvoid, uint32_t ledgerNum)
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
Benchmark::scheduleLoad(Application& app,
                        std::function<bool()> loadGenerator,
                        std::chrono::milliseconds stepTime)
{
    VirtualTimer& timer = getTimer(app.getClock());
    timer.expires_from_now(stepTime);
    timer.async_wait(
        [this, &app, loadGenerator, stepTime](asio::error_code const& error) {
            if (error)
            {
                return;
            }
            if (loadGenerator())
            {
                this->scheduleLoad(app, loadGenerator, stepTime);
            }
        });
}

VirtualTimer&
Benchmark::getTimer(VirtualClock& clock)
{
    if (!mLoadTimer)
    {
        mLoadTimer = make_unique<VirtualTimer>(clock);
    }
    return *mLoadTimer;
}

Benchmark::BenchmarkBuilder::BenchmarkBuilder(Hash const& networkID)
    : mInitialize(false), mPopulate(false), mTxRate(0), mAccounts(0), mNetworkID(networkID)
{
}

Benchmark::BenchmarkBuilder&
Benchmark::BenchmarkBuilder::setNumberOfInitialAccounts(uint32_t accounts)
{
    mAccounts = accounts;
    return *this;
}

Benchmark::BenchmarkBuilder&
Benchmark::BenchmarkBuilder::setTxRate(uint32_t txRate)
{
    mTxRate = txRate;
    return *this;
}

Benchmark::BenchmarkBuilder&
Benchmark::BenchmarkBuilder::initializeBenchmark()
{
    mInitialize = true;
    return *this;
}

Benchmark::BenchmarkBuilder&
Benchmark::BenchmarkBuilder::populateBenchmarkData()
{
    mPopulate = true;
    return *this;
}

std::unique_ptr<Benchmark>
Benchmark::BenchmarkBuilder::createBenchmark(Application& app) const
{
    auto sampler = make_unique<ShuffleLoadGenerator>(mNetworkID);
    if (mPopulate)
    {
        prepareBenchmark(app, *sampler);
    }
    if (mInitialize)
    {
        sampler->initialize(app, mAccounts);
    }
    setMaxTxSize(app.getLedgerManager(), MAXIMAL_NUMBER_OF_TXS_PER_LEDGER);
    app.getHerder().triggerNextLedger(app.getLedgerManager().getLedgerNum());
    app.getHistoryManager().queueCurrentHistory();

    class BenchmarkExt : public Benchmark {
    public:
        BenchmarkExt(uint32_t txRate, std::unique_ptr<TxSampler> sampler)
            : Benchmark(txRate, std::move(sampler))
            {}
    };
    std::unique_ptr<Benchmark> benchmark = make_unique<BenchmarkExt>(mTxRate, std::unique_ptr<TxSampler>(sampler.release()));

    return std::move(benchmark);
}

TxSampler::~TxSampler()
{
}
}
