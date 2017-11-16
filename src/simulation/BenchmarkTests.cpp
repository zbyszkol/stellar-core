// TODO base this on CoreTests.cpp.
#include "simulation/Benchmark.h"
#include "lib/catch.hpp"
#include "test/test.h"
#include "util/Timer.h"
#include "util/make_unique.h"
#include "main/PersistentState.h"
#include "history/HistoryArchive.h"
#include "util/Logging.h"
#include <memory>
#include "medida/metric_processor.h"
#include "medida/reporting/json_reporter.h"

using namespace stellar;

const char* LOGGER_ID = "Benchmark";

std::unique_ptr<Benchmark>
initializeBenchmark(Application& app)
{
    auto benchmark = make_unique<Benchmark>(app.getNetworkID());
    benchmark->initializeBenchmark(app, app.getLedgerManager().getLedgerNum() - 1);
    return benchmark;
}

void
prepareBenchmark(Application& app)
{
    auto benchmark = make_unique<Benchmark>(app.getNetworkID());
    benchmark->prepareBenchmark(app);
}

std::unique_ptr<Config>
initializeConfig()
{
    // HTTP_PORT=11626
    // PUBLIC_HTTP_PORT=true
    // RUN_STANDALONE=true
    // ARTIFICIALLY_GENERATE_LOAD_FOR_TESTING=true
    // DESIRED_MAX_TX_PER_LEDGER=1000

    // NETWORK_PASSPHRASE="Test SDF Network ; September 2015"

    // NODE_SEED="SDQVDISRYN2JXBS7ICL7QJAEKB3HWBJFP2QECXG7GZICAHBK4UNJCWK2 self"
    // NODE_IS_VALIDATOR=true

    // DATABASE="postgresql://dbname=core user=stellar password=__PGPASS__ host=localhost"
    // # DATABASE="sqlite3://stellar.db"

    // COMMANDS=["ll?level=info"]

    // FAILURE_SAFETY=0
    // UNSAFE_QUORUM=true
    // #The public keys of the Stellar testnet servers
    // [QUORUM_SET]
    // THRESHOLD_PERCENT=100
    // VALIDATORS=["$self"]

    // [HISTORY.vs]
    // get="cp /tmp/stellar-core/history/vs/{0} {1}"
    // put="cp {0} /tmp/stellar-core/history/vs/{1}"
    // mkdir="mkdir -p /tmp/stellar-core/history/vs/{0}"

    // std::unique_ptr<Config> cfg = make_unique<Config>();
    // cfg->load("stellar-core_benchmark.cfg");

    std::unique_ptr<Config> cfg = make_unique<Config>(getTestConfig());
    cfg->DATABASE = SecretValue{"postgresql://dbname=core user=stellar password=__PGPASS__ host=localhost"};
    cfg->PUBLIC_HTTP_PORT=true;
    cfg->COMMANDS.push_back("ll?level=info");
    cfg->DESIRED_MAX_TX_PER_LEDGER = Benchmark::MAXIMAL_NUMBER_OF_TXS_PER_LEDGER;
    cfg->FORCE_SCP = true;
    cfg->RUN_STANDALONE = false;
    cfg->BUCKET_DIR_PATH = "buckets";
    using namespace std;
    const string historyName = "benchmark";
    const string historyGetCmd = "cp history/vs/{0} {1}";
    const string historyPutCmd = "cp {0} history/vs/{1}";
    const string historyMkdirCmd = "mkdir -p history/vs/{0}";
    cfg->HISTORY[historyName] = make_shared<HistoryArchive>(historyName, historyGetCmd,
                                                            historyPutCmd, historyMkdirCmd);

    return cfg;
}

void
reportBenchmark(Benchmark::Metrics& metrics, Application& app)
{
    class ReportProcessor : public medida::MetricProcessor {
    public:
        virtual ~ReportProcessor() = default;
        virtual void Process(medida::Timer& timer) {
            count = timer.count();
        }

        std::uint64_t count;

    };
    using namespace std;
    // "ledger", "transaction", "apply"
    auto externalizedTxs = app.getMetrics().GetAllMetrics()[{"ledger", "transaction", "apply"}];
    ReportProcessor processor;
    externalizedTxs->Process(processor);
    auto txsExternalized = processor.count;

    CLOG(INFO, LOGGER_ID) << endl
                          << "Benchmark metrics:" << endl
                          << "  time spent: " << metrics.timeSpent.count() << endl
                          << "  txs submitted: " << metrics.txsCount.count() << endl
                          << "  txs externalized: " << txsExternalized << endl;

    medida::reporting::JsonReporter jr(app.getMetrics());
    CLOG(INFO, LOGGER_ID) <<   jr.Report() << endl;
}

TEST_CASE("stellar-core benchmark's initialization", "[benchmark][initialize]")
{
    std::unique_ptr<Config> cfg = initializeConfig();
    VirtualClock clock(VirtualClock::REAL_TIME);
    Application::pointer app = Application::create(clock, *cfg, false);
    // app->start();
    app->applyCfgCommands();
    prepareBenchmark(*app);
    app->getHistoryManager().queueCurrentHistory();
    // app->getHistoryManager().publishQueuedHistory();
}

TEST_CASE("stellar-core's benchmark", "[benchmark]")
{
    auto testDuration = std::chrono::seconds(10);

    VirtualClock clock(VirtualClock::REAL_TIME);
    std::unique_ptr<Config> cfg = initializeConfig();

    Application::pointer app = Application::create(clock, *cfg, false);
    app->applyCfgCommands();
    app->start();
    auto benchmark = initializeBenchmark(*app);
    bool done = false;

    VirtualTimer timer{clock};
    auto metrics = benchmark->startBenchmark(*app);
    timer.expires_from_now(testDuration);
    timer.async_wait([&benchmark, &done, &metrics](asio::error_code const& error) {
            metrics = benchmark->stopBenchmark(metrics);
            done = true;
        });

    while (!done)
    {
        clock.crank();
    }
    app->gracefulStop();

    reportBenchmark(*metrics, *app);
}
