#pragma once

#include "../app/Config.h"
#include "../collectors/CollectorEngine.h"

#include <string>
#include <vector>
#include <filesystem>

namespace KTDiag
{

class ReportGenerator
{
public:
    bool Generate(const std::filesystem::path& outputDir,
                  const Config& config,
                  const std::vector<CollectorResult>& results);

    std::wstring GetError() const { return m_error; }

private:
    void WriteHtmlHeader(std::wofstream& ofs, const std::wstring& productName);
    void WriteInfoSection(std::wofstream& ofs, const Config& config);
    void WriteResultsTable(std::wofstream& ofs, const std::vector<CollectorResult>& results);
    void WriteArtifactsList(std::wofstream& ofs, const std::filesystem::path& outputDir);
    void WriteHtmlFooter(std::wofstream& ofs);

    std::wstring m_error;
};

} // namespace KTDiag
