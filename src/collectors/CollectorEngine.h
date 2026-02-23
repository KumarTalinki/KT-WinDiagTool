#pragma once

#include "ICollector.h"
#include "../app/Config.h"

#include <vector>
#include <memory>
#include <string>
#include <set>
#include <filesystem>
#include <functional>
#include <mutex>

namespace KTDiag
{

struct CollectorResult
{
    std::wstring collectorName;
    std::wstring collectorId;
    bool success = false;
    std::wstring errorMessage;
    double elapsedSeconds = 0.0;
};

// Callback for overall engine progress: (completedCount, totalCount, currentCollectorName)
using EngineProgressCallback = std::function<void(int, int, const std::wstring&)>;

class CollectorEngine
{
public:
    CollectorEngine();
    ~CollectorEngine();

    // Register a collector to be run
    void AddCollector(std::unique_ptr<ICollector> collector);

    // Run all registered collectors (or filtered subset).
    // filteredIds: if non-empty, only run collectors whose Id() is in this set.
    // Returns overall success (true if all collectors succeeded).
    bool RunAll(const Config& config,
                const std::filesystem::path& outputDir,
                const std::set<std::wstring>& filteredIds,
                EngineProgressCallback engineCb,
                ProgressCallback collectorCb);

    // Request cancellation of running collectors
    void Cancel();

    // Get results from the last RunAll() call
    const std::vector<CollectorResult>& GetResults() const { return m_results; }

    // Get count of successful / failed collectors
    int SuccessCount() const;
    int FailureCount() const;

private:
    std::vector<std::unique_ptr<ICollector>> m_collectors;
    std::vector<CollectorResult> m_results;
    CancelToken m_cancelToken;
    std::mutex m_resultsMutex;
};

} // namespace KTDiag
