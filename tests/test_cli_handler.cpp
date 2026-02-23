#include <gtest/gtest.h>
#include "app/CLIHandler.h"
#include "util/Logger.h"

#include <Windows.h>

// Helper to convert string literals to wchar_t* argv array
class ArgBuilder
{
public:
    template<typename... Args>
    ArgBuilder(Args... args)
    {
        (AddArg(args), ...);
    }

    void AddArg(const wchar_t* arg)
    {
        m_storage.push_back(arg);
        // Rebuild pointers (invalidated by push_back)
        m_argv.clear();
        for (auto& s : m_storage)
            m_argv.push_back(const_cast<wchar_t*>(s.c_str()));
    }

    int argc() const { return static_cast<int>(m_argv.size()); }
    wchar_t** argv() { return m_argv.data(); }

private:
    std::vector<std::wstring> m_storage;
    std::vector<wchar_t*> m_argv;
};

class CLIHandlerTest : public ::testing::Test
{
protected:
    void SetUp() override
    {
        KTDiag::Logger::Instance().Initialize(L"KT-WinDiagTool-Test");
    }
};

TEST_F(CLIHandlerTest, HelpReturnsZero)
{
    KTDiag::CLIHandler cli;
    ArgBuilder args(L"KT-WinDiagTool.exe", L"--help");
    int result = cli.Run(args.argc(), args.argv());
    EXPECT_EQ(result, 0);
}

TEST_F(CLIHandlerTest, ShortHelpReturnsZero)
{
    KTDiag::CLIHandler cli;
    ArgBuilder args(L"KT-WinDiagTool.exe", L"-h");
    int result = cli.Run(args.argc(), args.argv());
    EXPECT_EQ(result, 0);
}

TEST_F(CLIHandlerTest, NoArgsShowsHelp)
{
    // No arguments → show help and return 0 (not an error)
    KTDiag::CLIHandler cli;
    ArgBuilder args(L"KT-WinDiagTool.exe");
    int result = cli.Run(args.argc(), args.argv());
    EXPECT_EQ(result, 0);
}

TEST_F(CLIHandlerTest, MissingConfigReturnsCritical)
{
    // A collection flag without --config → missing config error
    KTDiag::CLIHandler cli;
    ArgBuilder args(L"KT-WinDiagTool.exe", L"--quiet");
    int result = cli.Run(args.argc(), args.argv());
    EXPECT_EQ(result, 2);
}

TEST_F(CLIHandlerTest, UnknownArgReturnsCritical)
{
    KTDiag::CLIHandler cli;
    ArgBuilder args(L"KT-WinDiagTool.exe", L"--unknown-flag");
    int result = cli.Run(args.argc(), args.argv());
    EXPECT_EQ(result, 2);
}

TEST_F(CLIHandlerTest, InvalidWppLevelReturnsCritical)
{
    KTDiag::CLIHandler cli;
    ArgBuilder args(L"KT-WinDiagTool.exe", L"--config", L"test.json", L"--wpp-level", L"0");
    int result = cli.Run(args.argc(), args.argv());
    EXPECT_EQ(result, 2);
}

TEST_F(CLIHandlerTest, InvalidWppLevelHighReturnsCritical)
{
    KTDiag::CLIHandler cli;
    ArgBuilder args(L"KT-WinDiagTool.exe", L"--config", L"test.json", L"--wpp-level", L"6");
    int result = cli.Run(args.argc(), args.argv());
    EXPECT_EQ(result, 2);
}

TEST_F(CLIHandlerTest, InvalidConfigPathReturnsCritical)
{
    KTDiag::CLIHandler cli;
    ArgBuilder args(L"KT-WinDiagTool.exe", L"--config", L"C:\\nonexistent\\path.json", L"--quiet");
    int result = cli.Run(args.argc(), args.argv());
    EXPECT_EQ(result, 2);
}

TEST_F(CLIHandlerTest, UiArgIsUnknown)
{
    // --ui is handled by main.cpp before CLIHandler is called; if it somehow
    // reaches CLIHandler it should be treated as an unknown flag.
    KTDiag::CLIHandler cli;
    ArgBuilder args(L"KT-WinDiagTool.exe", L"--ui");
    int result = cli.Run(args.argc(), args.argv());
    EXPECT_EQ(result, 2);
}
