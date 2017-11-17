#pragma once

// Copyright 2017 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "main/Application.h"
#include "simulation/Benchmark.h"
#include <chrono>
#include <memory>

namespace stellar
{

class BenchmarkExecutor
{
  public:
    void executeBenchmark(Application& app,
                          std::shared_ptr<Benchmark> benchmark,
                          std::chrono::seconds testDuration);
    void reportBenchmark(Benchmark::Metrics& metrics,
                         medida::MetricsRegistry& metricsRegistry);

  private:
    static const char* LOGGER_ID;
};
}
