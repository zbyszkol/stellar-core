// TODO base this on CoreTests.cpp.
#include "simulation/Benchmark.h"
#include "lib/catch.hpp"
#include "test/test.h"
#include "util/Timer.h"

using namespace stellar;

std::unique_ptr<Benchmark>
initializeBenchmark(Application& app)
{
}

void
reportBenchmark(Benchmark& benchmark)
{
}

TEST_CASE("Bucket-list entries vs. write throughput", "[scalability][hide]")
{
    auto testDuration = std::chrono::seconds(3600);

    VirtualClock clock(VirtualClock::REAL_TIME);
    Config const& cfg = getTestConfig();

    Application::pointer app = Application::create(clock, cfg);

    app->start();

    bool done = false;
    auto benchmark = initializeBenchmark(*app);

    VirtualTimer timer{clock};
    timer.expires_from_now(testDuration);
    timer.async_wait([&benchmark, &done](asio::error_code const& error) {
            benchmark->stopBenchmark();
            done = true;
        });

    while (!done)
    {
        clock.crank();
    }
    app->gracefulStop();

    reportBenchmark(*benchmark);
}
