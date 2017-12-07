// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "simulation/Benchmark.h"
#include "lib/catch.hpp"
#include "simulation/Benchmark.h"
#include "test/test.h"
#include "util/Timer.h"
#include "util/make_unique.h"
#include <chrono>
#include <memory>

using namespace stellar;

std::unique_ptr<Config>
initializeConfig()
{
    std::unique_ptr<Config> cfg = make_unique<Config>(getTestConfig());
    // cfg->DATABASE = SecretValue{"postgresql://dbname=core user=stellar "
    //                             "password=__PGPASS__ host=localhost"};
    // cfg->PUBLIC_HTTP_PORT = true;
    // cfg->COMMANDS.push_back("ll?level=info");
    // cfg->DESIRED_MAX_TX_PER_LEDGER = 10000;
    // // cfg->FORCE_SCP = true;
    // cfg->RUN_STANDALONE = true;
    // cfg->BUCKET_DIR_PATH = "buckets";
    // cfg->NETWORK_PASSPHRASE = "Test SDF Network ; September 2015";

    // using namespace std;
    // const string historyName = "benchmark";
    // const string historyGetCmd = "cp history/vs/{0} {1}";
    // const string historyPutCmd = "cp {0} history/vs/{1}";
    // const string historyMkdirCmd = "mkdir -p history/vs/{0}";
    // cfg->HISTORY[historyName] = make_shared<HistoryArchive>(
    //     historyName, historyGetCmd, historyPutCmd, historyMkdirCmd);

    return cfg;
}

TEST_CASE("stellar-core benchmark's initialization", "[benchmark][initialize][hide]")
{
    const size_t nAccounts = 1000;
    std::unique_ptr<Config> cfg = initializeConfig();
    VirtualClock clock(VirtualClock::REAL_TIME);
    Application::pointer app = Application::create(clock, *cfg, false);
    app->applyCfgCommands();
    app->start();

    Benchmark::BenchmarkBuilder builder{app->getNetworkID()};
    builder.setNumberOfInitialAccounts(nAccounts)
           .populateBenchmarkData();
    std::unique_ptr<Benchmark> benchmark = builder.createBenchmark(*app);
    REQUIRE(benchmark);
    std::unique_ptr<TxSampler> sampler = builder.createSampler(*app);
    auto tx = sampler->createTransaction(nAccounts);
    REQUIRE(tx);
    REQUIRE(tx->execute(*app));
}

TEST_CASE("stellar-core's benchmark", "[benchmark][execute][hide]")
{
    const std::chrono::seconds testDuration(3);
    const size_t nAccounts = 1000;
    const uint32_t txRate = 100;

    std::unique_ptr<Config> cfg = initializeConfig();
    VirtualClock clock(VirtualClock::REAL_TIME);
    Application::pointer app = Application::create(clock, *cfg, false);
    app->applyCfgCommands();
    app->start();

    Benchmark::BenchmarkBuilder builder{app->getNetworkID()};
    builder.setNumberOfInitialAccounts(nAccounts)
           .populateBenchmarkData()
           .initializeBenchmark();
    bool done = false;
    BenchmarkExecutor executor;
    executor.setBenchmark(builder.createBenchmark(*app));
    executor.executeBenchmark(*app, testDuration, txRate, [&done](Benchmark::Metrics metrics) {
            done = true;
        });
    while (!done)
    {
        clock.crank();
    }
    app->gracefulStop();
}
