#include <gtest/gtest.h>
#include <Windows.h>
#include <objbase.h>

int main(int argc, char** argv)
{
    // Initialize COM for WMI tests
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();

    CoUninitialize();
    return result;
}
