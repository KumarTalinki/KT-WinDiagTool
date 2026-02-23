#pragma once

#include <Windows.h>
#include "ICollector.h"

namespace KTDiag
{

class DiskSpaceCollector : public ICollector
{
public:
    std::wstring Name() const override { return L"Disk Space"; }
    std::wstring Id() const override { return L"disk"; }

    bool Collect(const Config& config,
                 const std::filesystem::path& outputDir,
                 ProgressCallback cb,
                 CancelToken cancel) override;

    std::wstring GetError() const override { return m_error; }

private:
    static std::wstring FormatBytes(ULONGLONG bytes);
    static const wchar_t* DriveTypeToString(UINT driveType);

    std::wstring m_error;
};

} // namespace KTDiag
