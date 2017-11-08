// TODO base this on CoreTests.cpp.
#include "simulation/Benchmark.h"
#include "lib/catch.hpp"
#include "test/test.h"
#include "util/Timer.h"
#include "util/make_unique.h"

using namespace stellar;

std::unique_ptr<Benchmark>
initializeBenchmark(Application& app)
{
    auto benchmark = make_unique<Benchmark>(app.getNetworkID());
    benchmark->initializeBenchmark(app);
    return benchmark;
}

std::unique_ptr<Config>
initializeConfig()
{
//     HTTP_PORT=11626
//     PUBLIC_HTTP_PORT=true
//     RUN_STANDALONE=true
//     ARTIFICIALLY_GENERATE_LOAD_FOR_TESTING=true
//     DESIRED_MAX_TX_PER_LEDGER=1000

//     NETWORK_PASSPHRASE="Test SDF Network ; September 2015"

//     NODE_SEED="SDQVDISRYN2JXBS7ICL7QJAEKB3HWBJFP2QECXG7GZICAHBK4UNJCWK2 self"
//     NODE_IS_VALIDATOR=true

//     DATABASE="postgresql://dbname=core user=stellar password=__PGPASS__ host=localhost"
//     # DATABASE="sqlite3://stellar.db"

//     COMMANDS=["ll?level=info"]

//     FAILURE_SAFETY=0
//     UNSAFE_QUORUM=true
//     #The public keys of the Stellar testnet servers
//     [QUORUM_SET]
//     THRESHOLD_PERCENT=100
//     VALIDATORS=["$self"]

//     [HISTORY.vs]
//     get="cp /tmp/stellar-core/history/vs/{0} {1}"
//     put="cp {0} /tmp/stellar-core/history/vs/{1}"
//     mkdir="mkdir -p /tmp/stellar-core/history/vs/{0}"

    std::unique_ptr<Config> cfg = make_unique<Config>(getTestConfig());
    cfg->DATABASE = SecretValue{"postgresql://dbname=core user=stellar password=__PGPASS__ host=localhost"};
    return cfg;
}

void
reportBenchmark(Benchmark& benchmark)
{
}

TEST_CASE("stellar-core's benchmark", "[benchmark]")
{
    auto testDuration = std::chrono::seconds(3600);

    VirtualClock clock(VirtualClock::REAL_TIME);
    std::unique_ptr<Config> cfg = initializeConfig();

    Application::pointer app = Application::create(clock, *cfg);

    app->start();

    bool done = false;
    auto benchmark = initializeBenchmark(*app);
    benchmark->startBenchmark(*app);

    VirtualTimer timer{clock};
    timer.expires_from_now(testDuration);
    timer.async_wait([&benchmark, &done](asio::error_code const& error) {
            benchmark->stopBenchmark();
            done = true;
        });

    while (!done)
    {
        std::cout << "cranking" << std::endl;
        clock.crank();
    }
    app->gracefulStop();

    reportBenchmark(*benchmark);
}
