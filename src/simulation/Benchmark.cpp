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

const size_t Benchmark::MAXIMAL_NUMBER_OF_ACCOUNTS_IN_BATCH = 10000;

Benchmark::Benchmark(medida::MetricsRegistry& registry, uint32_t txRate,
                     std::unique_ptr<TxSampler> sampler)
    : mIsRunning(false)
    , mTxRate(txRate * LoadGenerator::STEP_MSECS / 1000)
    , mMetrics(initializeMetrics(registry))
    , mSampler(std::move(sampler))
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
    mBenchmarkTimeContext =
        make_unique<medida::TimerContext>(mMetrics.mBenchmarkTimer.TimeScope());
    scheduleLoad(app,
                 std::chrono::milliseconds{LoadGenerator::STEP_MSECS});
}

Benchmark::Metrics
Benchmark::initializeMetrics(medida::MetricsRegistry& registry)
{
    return Benchmark::Metrics(registry);
}

Benchmark::Metrics::Metrics(medida::MetricsRegistry& registry)
    : mBenchmarkTimer(registry.NewTimer({"benchmark", "overall", "time"}))
    , mTxsCount(registry.NewCounter({"benchmark", "txs", "count"}))
{
}

Benchmark::Metrics
Benchmark::stopBenchmark()
{
    LOG(INFO) << "Stopping benchmark";
    if (!mIsRunning)
    {
        throw std::runtime_error{"Benchmark is already stopped"};
    }
    mBenchmarkTimeContext->Stop();
    mIsRunning = false;
    LOG(INFO) << "Benchmark stopped";
    return mMetrics;
}

bool
Benchmark::generateLoadForBenchmark(Application& app)
{
    LOG(TRACE) << "Generating " << mTxRate
                           << " transaction(s) per step";

    mBenchmarkTimeContext->Stop();
    auto txs = mSampler->createTransaction(mTxRate);
    mBenchmarkTimeContext->Reset();
    if (!txs->execute(app))
    {
        LOG(ERROR) << "Error while executing a transaction: "
            "transaction was rejected";
        return false;
    }
    mMetrics.mTxsCount.inc(mTxRate);

    LOG(TRACE) << mTxRate
               << " transaction(s) generated in a single step";

    return true;
}

void
Benchmark::scheduleLoad(Application& app, std::chrono::milliseconds stepTime)
{
    if (!mLoadTimer)
    {
        mLoadTimer = make_unique<VirtualTimer>(app.getClock());
    }
    mLoadTimer->expires_from_now(stepTime);
    mLoadTimer->async_wait(
        [this, &app, stepTime](asio::error_code const& error) {
            if (error)
            {
                return;
            }
            if (generateLoadForBenchmark(app))
            {
                this->scheduleLoad(app, stepTime);
            }
        });
}

Benchmark::BenchmarkBuilder::BenchmarkBuilder(Hash const& networkID)
    : mInitialize(false)
    , mPopulate(false)
    , mTxRate(0)
    , mAccounts(0)
    , mNetworkID(networkID)
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

    struct BenchmarkExt : Benchmark
    {
        BenchmarkExt(medida::MetricsRegistry& registry, uint32_t txRate,
                     std::unique_ptr<TxSampler> sampler)
            : Benchmark(registry, txRate, std::move(sampler))
        {
        }
    };
    return make_unique<BenchmarkExt>(
        app.getMetrics(), mTxRate,
        std::move(sampler));
}

void
Benchmark::BenchmarkBuilder::prepareBenchmark(
    Application& app, ShuffleLoadGenerator& sampler) const
{
    populateAccounts(app, mAccounts, sampler);
}

void
Benchmark::BenchmarkBuilder::populateAccounts(
    Application& app, size_t size, ShuffleLoadGenerator& sampler) const
{
    for (size_t accountsLeft = size, batchSize = size; accountsLeft > 0;
         accountsLeft -= batchSize)
    {
        batchSize = std::min(accountsLeft, MAXIMAL_NUMBER_OF_ACCOUNTS_IN_BATCH);
        auto newAccounts = sampler.createAccounts(batchSize);
        createAccountsDirectly(app, newAccounts);
    }
}

void
Benchmark::BenchmarkBuilder::createAccountsDirectly(
    Application& app,
    std::vector<LoadGenerator::AccountInfoPtr>& createdAccounts) const
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
}

ShuffleLoadGenerator::ShuffleLoadGenerator(Hash const& networkID)
    : LoadGenerator(networkID)
{
}

std::unique_ptr<TxSampler::Tx>
ShuffleLoadGenerator::createTransaction(size_t size)
{
    struct LoadGeneratorTx : TxSampler::Tx
    {
        virtual bool
        execute(Application& app) override
        {
            for (auto& tx : mTxs)
            {
                if (!tx.execute(app))
                {
                    return false;
                }
            }
            return true;
        }

        std::vector<LoadGenerator::TxInfo> mTxs;
    };

    auto result = make_unique<LoadGeneratorTx>();
    for (size_t it = 0; it < size; ++it)
    {
        result->mTxs.push_back(LoadGenerator::createRandomTransaction(0.5));
    }
    return std::move(result);
}

std::vector<LoadGenerator::AccountInfoPtr>
ShuffleLoadGenerator::createAccounts(size_t batchSize)
{
    return LoadGenerator::createAccounts(batchSize);
}

void
ShuffleLoadGenerator::initialize(Application& app, size_t numberOfAccounts)
{
    LOG(INFO) << "Initializing benchmark";

    if (mAccounts.empty())
    {
        mAccounts = LoadGenerator::createAccounts(numberOfAccounts);
        loadAccounts(app, mAccounts);
    }
    mRandomIterator = shuffleAccounts(mAccounts);

    LOG(INFO) << "Benchmark initialized";
}

LoadGenerator::AccountInfoPtr
ShuffleLoadGenerator::pickRandomAccount(AccountInfoPtr tryToAvoid,
                                        uint32_t ledgerNum)
{
    if (mRandomIterator == mAccounts.end())
    {
        mRandomIterator = mAccounts.begin();
    }
    auto result = *mRandomIterator;
    mRandomIterator++;
    return result;
}

std::vector<LoadGenerator::AccountInfoPtr>::iterator
ShuffleLoadGenerator::shuffleAccounts(
    std::vector<LoadGenerator::AccountInfoPtr>& accounts)
{
    auto rng = std::default_random_engine{0};
    std::shuffle(mAccounts.begin(), mAccounts.end(), rng);
    return mAccounts.begin();
}

void
BenchmarkExecutor::executeBenchmark(
    Application& app, Benchmark::BenchmarkBuilder& benchmarkBuilder,
    std::chrono::seconds testDuration,
    std::function<void(Benchmark::Metrics)> stopCallback)
{
    if (!mLoadTimer)
    {
        mLoadTimer = make_unique<VirtualTimer>(app.getClock());
    }
    mLoadTimer->expires_from_now(std::chrono::milliseconds{1});
    mLoadTimer->async_wait([this, &app, benchmarkBuilder, testDuration,
                      stopCallback](asio::error_code const& error) {

        std::shared_ptr<Benchmark> benchmark{
            benchmarkBuilder.createBenchmark(app)};
        benchmark->startBenchmark(app);

        auto stopProcedure = [benchmark,
                              stopCallback](asio::error_code const& error) {

            auto metrics = benchmark->stopBenchmark();
            stopCallback(metrics);

            LOG(INFO) << "Benchmark complete.";
        };

        mLoadTimer->expires_from_now(testDuration);
        mLoadTimer->async_wait(stopProcedure);
    });
}
}
