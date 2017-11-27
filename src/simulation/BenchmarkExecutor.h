#pragma once

// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "main/Application.h"
#include "medida/metric_processor.h"
#include "medida/reporting/json_reporter.h"
#include "simulation/Benchmark.h"
#include "util/Timer.h"
#include <chrono>
#include <memory>

namespace stellar
{

class BenchmarkExecutor
{
  public:
    BenchmarkExecutor(std::unique_ptr<Benchmark> benchmark);

    void executeBenchmark(Application& app,
                          std::chrono::seconds testDuration);

  private:
    VirtualTimer& getTimer(VirtualClock& clock);

    std::unique_ptr<Benchmark> mBenchmark;
    std::unique_ptr<VirtualTimer> mLoadTimer;
    static const char* LOGGER_ID;
};

struct BenchmarkReporter
{
    template<typename Stream>
    void reportBenchmark(Benchmark::Metrics const& metrics,
                         medida::MetricsRegistry& metricsRegistry,
                         Stream& str)
        {
            using namespace std;
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
            auto externalizedTxs =
                metricsRegistry.GetAllMetrics()[{"ledger", "transaction", "apply"}];
            ReportProcessor processor;
            externalizedTxs->Process(processor);
            auto txsExternalized = processor.count;

            // CLOG(INFO, LOGGER_ID) << endl
            str << endl
                << "Benchmark metrics:" << endl
                << "  time spent: " << metrics.timeSpent.count()
                << " nanoseconds" << endl
                << "  txs submitted: " << metrics.txsCount.count()
                << endl
                << "  txs externalized: " << txsExternalized << endl;

            medida::reporting::JsonReporter jr(metricsRegistry);
            // CLOG(INFO, LOGGER_ID) << jr.Report() << endl;
            str << jr.Report() << endl;
        }
};
}
