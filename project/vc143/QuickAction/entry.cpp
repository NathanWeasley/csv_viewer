#include "quick_action.h"

#include <Windows.h>

#include <fcntl.h>
#include <io.h>

#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace
{

std::wstring fromUtf8(std::string_view text)
{
    if (text.empty())
        return {};
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (required <= 0)
        return std::wstring(text.begin(), text.end());
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                        static_cast<int>(text.size()), result.data(), required);
    return result;
}

void printUsage()
{
    std::wcout
        << L"QuickAction - extract Viewer ZIP logs\n\n"
        << L"Usage:\n"
        << L"  QuickAction.exe <log.zip>\n\n"
        << L"The output is written to a directory beside the ZIP, using the ZIP file name\n"
        << L"without its extension. HikLog files become CSV, RBT logs become TXT, and\n"
        << L"ROBOT_CAPA/ROBOT_CALIB/ROBOT_CONFIG DAT files become JSON.\n";
}

} // namespace

int wmain(int argc, wchar_t* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    _setmode(_fileno(stdout), _O_U8TEXT);
    _setmode(_fileno(stderr), _O_U8TEXT);

    if (argc == 2 && (std::wstring_view(argv[1]) == L"--help"
                      || std::wstring_view(argv[1]) == L"-h"
                      || std::wstring_view(argv[1]) == L"/?"))
    {
        printUsage();
        return 0;
    }
    if (argc != 2)
    {
        printUsage();
        return 2;
    }

    const std::filesystem::path archivePath(argv[1]);
    quickaction::RunResult result;
    try
    {
        result = quickaction::convertArchive(archivePath);
    }
    catch (const std::exception& error)
    {
        std::wcerr << L"ERROR: unexpected conversion failure: "
                   << fromUtf8(error.what()) << L'\n';
        return 1;
    }
    if (!result.archiveError.empty())
    {
        std::wcerr << L"ERROR: " << fromUtf8(result.archiveError) << L'\n';
        return 1;
    }

    std::wcout << L"Output: " << result.outputDirectory.wstring() << L'\n';
    for (const auto& item : result.items)
    {
        std::wcout << (item.success ? L"[OK]   " : L"[FAIL] ")
                   << fromUtf8(item.type) << L"  " << fromUtf8(item.sourcePath);
        if (!item.outputPath.empty())
            std::wcout << L" -> " << item.outputPath.wstring();
        if (!item.detail.empty())
            std::wcout << L"  (" << fromUtf8(item.detail) << L')';
        std::wcout << L'\n';
    }

    std::wcout << L"Summary: HikLog " << result.hiklogSuccess << L'/' << result.hiklogCount
               << L", RBT " << result.rbtSuccess << L'/' << result.rbtCount
               << L", DAT " << result.datSuccess << L'/' << result.datCount
               << L", failed " << result.failureCount() << L'\n';
    return result.success() ? 0 : 1;
}
