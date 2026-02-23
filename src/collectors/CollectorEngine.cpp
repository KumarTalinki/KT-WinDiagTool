#include "CollectorEngine.h"
#include "../util/Logger.h"

#include <future>
#include <chrono>
#include <algorithm>

namespace KTDiag
{

CollectorEngine::CollectorEngine()
    : m_cancelToken(std::make_shared<std::atomic<bool>>(false))
{
}

CollectorEngine::~CollectorEngine() = default;

void CollectorEngine::AddCollector(std::unique_ptr<ICollector> collector)
{
    m_collectors.push_back(std::move(collector));
}

bool CollectorEngine::RunAll(const Config& config,
                              const std::filesystem::path& outputDir,
                              const std::set<std::wstring>& filteredIds,
                              EngineProgressCallback engineCb,
                              ProgressCallback collectorCb)
{
    namespace fs = std::filesystem;

    m_results.clear();
    m_cancelToken->store(false);

    // Create output directory
    std::error_code ec;
    fs::create_directories(outputDir, ec);
    if (ec)
    {
        Logger::Instance().Log(LogLevel::Error, L"Failed to create output directory: %s",
                               outputDir.wstring().c_str());
        return false;
    }

    // Build list of collectors to run
    std::vector<ICollector*> toRun;
    for (auto& c : m_collectors)
    {
        if (filteredIds.empty() || filteredIds.count(c->Id()) > 0)
            toRun.push_back(c.get());
    }

    if (toRun.empty())
    {
        Logger::Instance().Log(LogLevel::Warning, L"No collectors selected to run");
        return true;
    }

    int totalCount = static_cast<int>(toRun.size());
    Logger::Instance().Log(LogLevel::Info, L"Running %d collector(s)...", totalCount);

    // Launch collectors in parallel using std::async
    struct AsyncTask
    {
        ICollector* collector;
        std::future<CollectorResult> future;
    };

    std::vector<AsyncTask> tasks;
    tasks.reserve(toRun.size());

    for (auto* collector : toRun)
    {
        // Create per-collector output subdirectory
        fs::path collectorDir = outputDir / collector->Id();
        fs::create_directories(collectorDir, ec);

        auto cancel = m_cancelToken;
        auto cb = collectorCb;

        tasks.push_back({collector, std::async(std::launch::async,
            [collector, &config, collectorDir, cb, cancel]() -> CollectorResult
            {
                CollectorResult result;
                result.collectorName = collector->Name();
                result.collectorId = collector->Id();

                auto start = std::chrono::steady_clock::now();

                try
                {
                    Logger::Instance().Log(LogLevel::Info, L"[%s] Starting collection...",
                                           result.collectorName.c_str());

                    result.success = collector->Collect(config, collectorDir, cb, cancel);

                    if (!result.success)
                        result.errorMessage = collector->GetError();
                }
                catch (const std::exception& e)
                {
                    result.success = false;
                    result.errorMessage = L"Exception: " +
                        std::wstring(e.what(), e.what() + strlen(e.what()));
                }
                catch (...)
                {
                    result.success = false;
                    result.errorMessage = L"Unknown exception";
                }

                auto end = std::chrono::steady_clock::now();
                result.elapsedSeconds = std::chrono::duration<double>(end - start).count();

                if (result.success)
                {
                    Logger::Instance().Log(LogLevel::Info, L"[%s] Completed in %.1fs",
                                           result.collectorName.c_str(), result.elapsedSeconds);
                }
                else
                {
                    Logger::Instance().Log(LogLevel::Error, L"[%s] Failed: %s (%.1fs)",
                                           result.collectorName.c_str(),
                                           result.errorMessage.c_str(),
                                           result.elapsedSeconds);
                }

                return result;
            })
        });
    }

    // Wait for all tasks and collect results
    int completedCount = 0;
    for (auto& task : tasks)
    {
        CollectorResult result = task.future.get();
        ++completedCount;

        if (engineCb)
            engineCb(completedCount, totalCount, result.collectorName);

        std::lock_guard<std::mutex> lock(m_resultsMutex);
        m_results.push_back(std::move(result));
    }

    int failures = FailureCount();
    Logger::Instance().Log(LogLevel::Info, L"Collection complete: %d/%d succeeded, %d failed",
                           SuccessCount(), totalCount, failures);

    return failures == 0;
}

void CollectorEngine::Cancel()
{
    m_cancelToken->store(true);
    Logger::Instance().Log(LogLevel::Warning, L"Collection cancellation requested");
}

int CollectorEngine::SuccessCount() const
{
    return static_cast<int>(std::count_if(m_results.begin(), m_results.end(),
                                          [](const CollectorResult& r) { return r.success; }));
}

int CollectorEngine::FailureCount() const
{
    return static_cast<int>(std::count_if(m_results.begin(), m_results.end(),
                                          [](const CollectorResult& r) { return !r.success; }));
}

} // namespace KTDiag
