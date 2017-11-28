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
#include <memory>

namespace stellar
{

const char* BenchmarkExecutor::LOGGER_ID = Benchmark::LOGGER_ID;

void
BenchmarkExecutor::executeBenchmark(Application& app,
                                    std::shared_ptr<Benchmark> benchmark,
                                    std::chrono::seconds testDuration)
{
    VirtualTimer& timer = getTimer(app.getClock());

    app.getMetrics()
        .NewMeter({"benchmark", "run", "started"}, "run")
        .Mark();

    auto stopProcedure = [this, &app, benchmark](asio::error_code const& error) {

        auto metrics = benchmark->stopBenchmark();
        BenchmarkReporter().reportBenchmark(metrics, app.getMetrics(), CLOG(INFO, LOGGER_ID));

        app.getMetrics().NewMeter({"benchmark", "run", "complete"}, "run").Mark();

        CLOG(INFO, LOGGER_ID) << "Benchmark complete.";
    };
    timer.expires_from_now(std::chrono::milliseconds{1});
    timer.async_wait(
        [this, &app, benchmark, testDuration, stopProcedure, &timer](asio::error_code const& error) {
            benchmark->initializeBenchmark(app);
            benchmark->startBenchmark(app);

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
