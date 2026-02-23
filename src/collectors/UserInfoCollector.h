#pragma once

#include "ICollector.h"

namespace KTDiag
{

class UserInfoCollector : public ICollector
{
public:
    std::wstring Name() const override { return L"User Information"; }
    std::wstring Id() const override { return L"userinfo"; }

    bool Collect(const Config& config,
                 const std::filesystem::path& outputDir,
                 ProgressCallback cb,
                 CancelToken cancel) override;

    std::wstring GetError() const override { return m_error; }

private:
    std::wstring m_error;
};

} // namespace KTDiag
