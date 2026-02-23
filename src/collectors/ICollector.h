#pragma once

#include "../app/Config.h"

#include <string>
#include <filesystem>
#include <functional>
#include <atomic>

namespace KTDiag
{

// Progress callback: (collectorName, statusMessage, percentComplete 0-100)
using ProgressCallback = std::function<void(const std::wstring&, const std::wstring&, int)>;

// Cancellation token shared between engine and collectors
using CancelToken = std::shared_ptr<std::atomic<bool>>;

class ICollector
{
public:
    virtual ~ICollector() = default;

    // Display name for this collector (e.g., "Environment Variables")
    virtual std::wstring Name() const = 0;

    // Short identifier used in CLI --collectors flag (e.g., "env")
    virtual std::wstring Id() const = 0;

    // Run the collection. Returns true on success, false on failure.
    // outputDir: directory where this collector should write its output files
    // cb: optional progress callback
    // cancel: if set to true externally, collector should abort gracefully
    virtual bool Collect(const Config& config,
                         const std::filesystem::path& outputDir,
                         ProgressCallback cb,
                         CancelToken cancel) = 0;

    // Get error description if Collect() returned false
    virtual std::wstring GetError() const = 0;
};

} // namespace KTDiag
