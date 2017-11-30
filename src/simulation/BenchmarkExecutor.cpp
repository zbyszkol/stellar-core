// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "simulation/BenchmarkExecutor.h"

#include "main/Application.h"
#include "medida/metric_processor.h"
#include "medida/reporting/json_reporter.h"
#include "simulation/Benchmark.h"
#include "util/Logging.h"
#include "util/Timer.h"
#include "util/make_unique.h"
#include <chrono>
#include <functional>
#include <memory>

namespace stellar
{

const char* BenchmarkExecutor::LOGGER_ID = Benchmark::LOGGER_ID;

void
BenchmarkExecutor::executeBenchmark(Application& app,
                                    Benchmark::BenchmarkBuilder benchmarkBuilder,
                                    std::chrono::seconds testDuration,
                                    std::function<void(Benchmark::Metrics)> stopCallback)
{
    VirtualTimer& timer = getTimer(app.getClock());
    timer.expires_from_now(std::chrono::milliseconds{1});
    timer.async_wait(
        [this, &app, benchmarkBuilder, testDuration, &timer, stopCallback](asio::error_code const& error) {

            std::shared_ptr<Benchmark> benchmark{benchmarkBuilder.createBenchmark(app)};
            benchmark->startBenchmark(app);

            auto stopProcedure = [benchmark, stopCallback](asio::error_code const& error) {

                auto metrics = benchmark->stopBenchmark();
                stopCallback(metrics);

                CLOG(INFO, LOGGER_ID) << "Benchmark complete.";
            };

            timer.expires_from_now(testDuration);
            timer.async_wait(stopProcedure);
        });
}

VirtualTimer&
BenchmarkExecutor::getTimer(VirtualClock& clock)
{
    if (!mLoadTimer)
    {
        mLoadTimer = make_unique<VirtualTimer>(clock);
    }
    return *mLoadTimer;
}
}
