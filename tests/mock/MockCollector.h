#pragma once

#include "collectors/ICollector.h"

#include <string>
#include <fstream>
#include <thread>
#include <chrono>

namespace KTDiag
{

// Configurable mock collector for engine tests
class MockCollector : public ICollector
{
public:
    MockCollector(const std::wstring& name, const std::wstring& id,
                  bool shouldSucceed = true, int sleepMs = 0)
        : m_name(name), m_id(id), m_shouldSucceed(shouldSucceed), m_sleepMs(sleepMs)
    {
    }

    std::wstring Name() const override { return m_name; }
    std::wstring Id() const override { return m_id; }

    bool Collect(const Config& /*config*/,
                 const std::filesystem::path& outputDir,
                 ProgressCallback cb,
                 CancelToken cancel) override
    {
        UNREFERENCED_PARAMETER(cb);

        if (cancel && cancel->load())
        {
            m_error = L"Cancelled";
            return false;
        }

        // Simulate work with configurable delay
        if (m_sleepMs > 0)
        {
            for (int i = 0; i < m_sleepMs; i += 10)
            {
                if (cancel && cancel->load())
                {
                    m_error = L"Cancelled";
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        m_collected = true;

        // Write a marker file to prove we ran
        std::filesystem::path markerFile = outputDir / (m_id + L"_marker.txt");
        std::wofstream ofs(markerFile);
        if (ofs.is_open())
            ofs << L"MockCollector " << m_name << L" ran successfully\n";

        if (!m_shouldSucceed)
        {
            m_error = L"Mock failure: " + m_name;
            return false;
        }

        return true;
    }

    std::wstring GetError() const override { return m_error; }
    bool WasCollected() const { return m_collected; }

private:
    std::wstring m_name;
    std::wstring m_id;
    bool m_shouldSucceed;
    int m_sleepMs;
    std::wstring m_error;
    bool m_collected = false;
};

} // namespace KTDiag
