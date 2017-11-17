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
    benchmark->prepareBenchmark(app);

    VirtualClock clock(VirtualClock::REAL_TIME);
    VirtualTimer timer{clock};
    auto metrics = benchmark->startBenchmark(app);
    timer.expires_from_now(testDuration);
    timer.async_wait(
        [this, benchmark, &metrics, &app](asio::error_code const& error) {

            metrics = benchmark->stopBenchmark(metrics);
            reportBenchmark(*metrics, app.getMetrics());

            app.getMetrics()
                .NewMeter({"benchmark", "run", "complete"}, "run")
                .Mark();
            CLOG(INFO, LOGGER_ID) << "Benchmark complete.";
        });
}

void
BenchmarkExecutor::reportBenchmark(Benchmark::Metrics& metrics,
                                   medida::MetricsRegistry& metricsRegistry)
{
    class ReportProcessor : public medida::MetricProcessor
    {
      public:
        virtual ~ReportProcessor() = default;
        virtual void
        Process(medida::Timer& timer)
        {
            count = timer.count();
        }

        std::uint64_t count;
    };
    using namespace std;
    auto externalizedTxs =
        metricsRegistry.GetAllMetrics()[{"ledger", "transaction", "apply"}];
    ReportProcessor processor;
    externalizedTxs->Process(processor);
    auto txsExternalized = processor.count;

    CLOG(INFO, LOGGER_ID) << endl
                          << "Benchmark metrics:" << endl
                          << "  time spent: " << metrics.timeSpent.count()
                          << " nanoseconds" << endl
                          << "  txs submitted: " << metrics.txsCount.count()
                          << endl
                          << "  txs externalized: " << txsExternalized << endl;

    medida::reporting::JsonReporter jr(metricsRegistry);
    CLOG(INFO, LOGGER_ID) << jr.Report() << endl;
}
}
