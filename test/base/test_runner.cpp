#include "test_registry.h"

#include <iostream>
#include <algorithm>
#include <string>

namespace {

// ============================================================
// ANSI color codes
// ============================================================
constexpr const char* RESET  = "\033[0m";
constexpr const char* GREEN  = "\033[32m";
constexpr const char* RED    = "\033[31m";
constexpr const char* BOLD   = "\033[1m";
constexpr const char* YELLOW = "\033[33m";

// Helper: wrap a string in ANSI colors
std::string green(const std::string& s)  { return GREEN + s + RESET; }
std::string red(const std::string& s)    { return RED   + s + RESET; }
std::string bold(const std::string& s)   { return BOLD  + s + RESET; }

// ============================================================
// Formatting helpers
// ============================================================

constexpr int NAME_WIDTH = 64;

void printSeparator(char ch = '-')
{
    std::cout << std::string(80, ch) << std::endl;
}

std::string formatTestName(const std::string& group, const std::string& name)
{
    std::string full = group + "::" + name;
    if (full.size() > static_cast<size_t>(NAME_WIDTH))
        return full.substr(0, NAME_WIDTH - 3) + "...";
    return full + std::string(NAME_WIDTH - full.size(), ' ');
}

} // anonymous namespace

int main()
{
    const auto& entries = test::detail::getAllTests();

    std::cout << "\n";
    printSeparator('=');
    std::cout << "  " << bold("CSV Viewer Unit Tests") << std::endl;
    std::cout << "  Total test cases: " << entries.size() << std::endl;
    printSeparator('=');
    std::cout << std::endl;

    std::vector<test::TestResult> results;
    size_t passedCount = 0;
    size_t failedCount = 0;

    for (const auto& entry : entries)
    {
        test::TestResult result;
        result.groupName = entry.group;
        result.testName  = entry.name;
        result.passed    = true;

        try
        {
            entry.testFunc();
        }
        catch (const test::TestFailure& e)
        {
            result.passed = false;
            result.detail = e.what();
        }
        catch (const std::exception& e)
        {
            result.passed = false;
            result.detail = std::string("Unexpected exception: ") + e.what();
        }
        catch (...)
        {
            result.passed = false;
            result.detail = "Unknown exception";
        }

        if (result.passed)
        {
            ++passedCount;
            std::cout << "  " << green("[PASS]") << " " << formatTestName(result.groupName, result.testName) << std::endl;
        }
        else
        {
            ++failedCount;
            std::cout << "  " << red("[FAIL]") << " " << formatTestName(result.groupName, result.testName) << std::endl;
        }

        results.push_back(std::move(result));
    }

    // Failure details
    if (failedCount > 0)
    {
        std::cout << std::endl;
        printSeparator('=');
        std::cout << "  " << red("FAILURE DETAILS") << std::endl;
        printSeparator('=');
        for (const auto& r : results)
        {
            if (!r.passed)
            {
                std::cout << "\n  " << r.groupName << "::" << r.testName << std::endl;
                std::cout << "  " << std::string(60, '~') << std::endl;
                std::cout << "  " << r.detail << std::endl;
            }
        }
    }

    // Summary
    std::cout << std::endl;
    printSeparator('=');
    std::cout << "  " << bold("SUMMARY") << std::endl;
    printSeparator('=');
    std::cout << "  Total:  " << entries.size() << std::endl;
    std::cout << "  Passed: " << green(std::to_string(passedCount)) << std::endl;
    if (failedCount > 0)
        std::cout << "  Failed: " << red(std::to_string(failedCount)) << std::endl;
    else
        std::cout << "  Failed: " << failedCount << std::endl;

    if (failedCount == 0)
        std::cout << "\n  " << green("ALL TESTS PASSED!") << std::endl;
    else
        std::cout << "\n  " << red("SOME TESTS FAILED!") << std::endl;

    printSeparator('=');
    std::cout << std::endl;

    return failedCount > 0 ? 1 : 0;
}