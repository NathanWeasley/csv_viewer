#include "quick_action.h"

#include "code_logparse/binary_log_parser.h"
#include "code_logparse/rbt_log_parser.h"
#include "code_logparse/ziplog/zip_archive.h"
#include "plugins-dev/dat_decrypt/code/dat_converter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <set>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace quickaction
{
namespace
{

using viewer::logparse::DiagnosticSeverity;
using viewer::logparse::ParseResult;
using viewer::logparse::ziplog::ZipArchive;
using viewer::logparse::ziplog::ZipEntryInfo;

constexpr std::uint64_t kMaximumDatSize = 128ULL * 1024ULL * 1024ULL;

std::string asciiLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character)
    {
        return character >= 'A' && character <= 'Z'
            ? static_cast<char>(character - 'A' + 'a')
            : static_cast<char>(character);
    });
    return value;
}

std::wstring pathKey(const std::filesystem::path& path)
{
    std::wstring key = path.lexically_normal().generic_wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t character)
    {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return key;
}

std::string fileNameUtf8(const ZipEntryInfo& entry)
{
    return std::filesystem::u8path(entry.pathUtf8).filename().u8string();
}

bool isHiklog(const ZipEntryInfo& entry)
{
    return asciiLower(std::filesystem::u8path(entry.pathUtf8).extension().u8string())
        == ".hiklog";
}

bool isRbtLog(const ZipEntryInfo& entry)
{
    const std::string name = fileNameUtf8(entry);
    return name.size() >= 3 && name.compare(0, 3, "RBT") == 0;
}

const char* expectedDatTable(const ZipEntryInfo& entry)
{
    const std::string name = asciiLower(fileNameUtf8(entry));
    if (name == "robot_capa.dat")
        return "ROBOT_CAPA";
    if (name == "robot_calib.dat")
        return "RIU_CALIB_PARA";
    if (name == "robot_config.dat")
        return "ROBOT_CONFIG";
    return nullptr;
}

std::filesystem::path outputPathFor(const std::filesystem::path& root,
                                    const ZipEntryInfo& entry,
                                    const char* extension)
{
    std::filesystem::path relative = std::filesystem::u8path(entry.pathUtf8);
    relative.replace_extension(extension);
    return root / relative;
}

bool createParentDirectory(const std::filesystem::path& path, std::string& error)
{
    std::error_code fileError;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent, fileError);
    if (fileError)
    {
        error = "unable to create output directory: " + fileError.message();
        return false;
    }
    return true;
}

void removePartialFile(const std::filesystem::path& path)
{
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
}

void writeCsvCell(std::ostream& output, const std::string& text)
{
    if (text.find_first_of(",\"\r\n") == std::string::npos)
    {
        output << text;
        return;
    }
    output.put('"');
    for (const char character : text)
    {
        if (character == '"')
            output.put('"');
        output.put(character);
    }
    output.put('"');
}

struct CsvColumn
{
    bool isString = false;
    std::size_t index = 0;
};

std::vector<CsvColumn> orderedColumns(const ParseResult& result)
{
    std::vector<CsvColumn> columns;
    columns.reserve(result.columns.size() + result.stringColumns.size());
    std::vector<bool> numericUsed(result.columns.size(), false);
    std::vector<bool> stringUsed(result.stringColumns.size(), false);

    for (const auto& packet : result.packetTypes)
    {
        for (const auto& field : packet.fields)
        {
            for (const std::size_t index : field.columnIndices)
            {
                if (index < numericUsed.size() && !numericUsed[index])
                {
                    numericUsed[index] = true;
                    columns.push_back({false, index});
                }
            }
            for (const std::size_t index : field.stringColumnIndices)
            {
                if (index < stringUsed.size() && !stringUsed[index])
                {
                    stringUsed[index] = true;
                    columns.push_back({true, index});
                }
            }
        }
    }
    for (std::size_t index = 0; index < numericUsed.size(); ++index)
    {
        if (!numericUsed[index])
            columns.push_back({false, index});
    }
    for (std::size_t index = 0; index < stringUsed.size(); ++index)
    {
        if (!stringUsed[index])
            columns.push_back({true, index});
    }
    return columns;
}

bool writeParsedCsv(const ParseResult& result,
                    const std::filesystem::path& outputPath,
                    std::string& error)
{
    if (!createParentDirectory(outputPath, error))
        return false;

    const std::vector<CsvColumn> columns = orderedColumns(result);
    for (const CsvColumn& column : columns)
    {
        const std::size_t rowCount = column.isString
            ? result.stringColumns[column.index].values.size()
            : result.columns[column.index].values.size();
        if (rowCount != result.timestampCount)
        {
            error = "parsed HikLog columns have inconsistent row counts";
            return false;
        }
    }

    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        error = "unable to create CSV output";
        return false;
    }
    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10);

    for (std::size_t index = 0; index < columns.size(); ++index)
    {
        if (index != 0)
            output.put(',');
        const CsvColumn& column = columns[index];
        writeCsvCell(output, column.isString
            ? result.stringColumns[column.index].name
            : result.columns[column.index].name);
    }
    output.put('\n');

    for (std::size_t row = 0; row < result.timestampCount; ++row)
    {
        for (std::size_t index = 0; index < columns.size(); ++index)
        {
            if (index != 0)
                output.put(',');
            const CsvColumn& column = columns[index];
            if (column.isString)
            {
                writeCsvCell(output, result.stringColumns[column.index].values[row]);
                continue;
            }

            const double value = result.columns[column.index].values[row];
            if (std::isnan(value))
                output << "NaN";
            else if (std::isinf(value))
                output << (value < 0.0 ? "-Inf" : "Inf");
            else
                output << value;
        }
        output.put('\n');
    }
    output.close();
    if (!output)
    {
        error = "failed while writing CSV output";
        removePartialFile(outputPath);
        return false;
    }
    return true;
}

std::string parserFailure(const ParseResult& result)
{
    std::ostringstream message;
    bool first = true;
    for (const auto& diagnostic : result.diagnostics)
    {
        if (diagnostic.severity != DiagnosticSeverity::Error)
            continue;
        if (!first)
            message << "; ";
        message << diagnostic.message << " at byte " << diagnostic.byteOffset;
        first = false;
    }
    return first ? "HikLog parser did not produce a valid schema" : message.str();
}

std::size_t warningCount(const ParseResult& result)
{
    return static_cast<std::size_t>(std::count_if(
        result.diagnostics.begin(), result.diagnostics.end(), [](const auto& diagnostic)
        {
            return diagnostic.severity == DiagnosticSeverity::Warning;
        }));
}

bool readEntry(ZipArchive& archive,
               const ZipEntryInfo& entry,
               std::vector<std::uint8_t>& bytes,
               std::string& error)
{
    if (entry.uncompressedSize > kMaximumDatSize
        || entry.uncompressedSize > static_cast<std::uint64_t>(bytes.max_size()))
    {
        error = "DAT entry exceeds the 128 MiB safety limit";
        return false;
    }
    std::unique_ptr<viewer::logparse::BinaryInput> input = archive.createInput(entry.index);
    if (!input)
    {
        error = "unable to create ZIP entry reader";
        return false;
    }
    if (!input->open())
    {
        error = "unable to open ZIP entry: " + input->lastError();
        return false;
    }
    bytes.resize(static_cast<std::size_t>(entry.uncompressedSize));
    if (!bytes.empty() && !input->read(bytes.data(), bytes.size()))
    {
        error = "unable to read ZIP entry: " + input->lastError();
        input->close();
        bytes.clear();
        return false;
    }
    input->close();
    return true;
}

bool writeTextFile(const std::filesystem::path& path,
                   const std::string& content,
                   std::string& error)
{
    if (!createParentDirectory(path, error))
        return false;
    if (content.size() > static_cast<std::size_t>(
            std::numeric_limits<std::streamsize>::max()))
    {
        error = "JSON output is too large";
        return false;
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
    {
        error = "unable to create JSON output";
        return false;
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.close();
    if (!output)
    {
        error = "failed while writing JSON output";
        removePartialFile(path);
        return false;
    }
    return true;
}

ItemResult convertHiklog(ZipArchive& archive,
                         const ZipEntryInfo& entry,
                         const std::filesystem::path& outputPath)
{
    ItemResult item{"HikLog", entry.pathUtf8, outputPath};
    auto input = archive.createInput(entry.index);
    if (!input)
    {
        item.detail = "ZIP entry is not readable";
        return item;
    }
    std::vector<std::unique_ptr<viewer::logparse::BinaryInput>> inputs;
    inputs.push_back(std::move(input));
    const ParseResult parsed = viewer::logparse::BinaryLogParser{}.parseInputs(std::move(inputs));
    if (!parsed.success())
    {
        item.detail = parserFailure(parsed);
        return item;
    }
    if (!writeParsedCsv(parsed, outputPath, item.detail))
        return item;

    const std::size_t warnings = warningCount(parsed);
    item.success = true;
    item.detail = std::to_string(parsed.timestampCount) + " rows, "
        + std::to_string(parsed.columns.size() + parsed.stringColumns.size()) + " columns";
    if (warnings != 0)
        item.detail += ", " + std::to_string(warnings) + " warnings";
    return item;
}

ItemResult convertRbt(ZipArchive& archive,
                      const ZipEntryInfo& entry,
                      const std::filesystem::path& outputPath)
{
    ItemResult item{"RBT", entry.pathUtf8, outputPath};
    if (!createParentDirectory(outputPath, item.detail))
        return item;
    auto input = archive.createInput(entry.index);
    if (!input)
    {
        item.detail = "ZIP entry is not readable";
        return item;
    }
    const auto parsed = viewer::logparse::RbtLogParser{}.parse(*input, outputPath);
    if (!parsed.success())
    {
        item.detail = parsed.error.empty() ? "RBT parser failed" : parsed.error;
        return item;
    }
    item.success = true;
    item.detail = std::to_string(parsed.blockCount) + " blocks, "
        + std::to_string(parsed.outputBytes) + " output bytes";
    return item;
}

ItemResult convertDat(ZipArchive& archive,
                      const ZipEntryInfo& entry,
                      const std::filesystem::path& outputPath,
                      std::string_view expectedTable)
{
    ItemResult item{"DAT", entry.pathUtf8, outputPath};
    std::vector<std::uint8_t> bytes;
    if (!readEntry(archive, entry, bytes, item.detail))
        return item;

    datconv::DatConverter converter;
    const auto status = converter.parse(
        std::move(bytes), datconv::DatConverter::ParseOptions(false, false, true));
    if (!status)
    {
        item.detail = std::string(datconv::errorCodeName(status.code)) + ": " + status.message;
        return item;
    }
    if (converter.tableName() != expectedTable)
    {
        item.detail = "expected DAT table '" + std::string(expectedTable)
            + "', found '" + std::string(converter.tableName()) + "'";
        return item;
    }

    std::string json;
    const auto jsonStatus = converter.toDocumentJson(json);
    if (!jsonStatus)
    {
        item.detail = std::string(datconv::errorCodeName(jsonStatus.code))
            + ": " + jsonStatus.message;
        return item;
    }
    if (!writeTextFile(outputPath, json, item.detail))
        return item;

    item.success = true;
    item.detail = std::to_string(converter.recordCount()) + " records, "
        + std::to_string(converter.diagnostics().size()) + " diagnostics";
    return item;
}

} // namespace

std::size_t RunResult::failureCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        items.begin(), items.end(), [](const ItemResult& item) { return !item.success; }));
}

bool RunResult::success() const noexcept
{
    return archiveError.empty() && !items.empty() && failureCount() == 0;
}

RunResult convertArchive(const std::filesystem::path& archivePath)
{
    RunResult result;
    std::error_code fileError;
    if (!std::filesystem::is_regular_file(archivePath, fileError))
    {
        result.archiveError = fileError
            ? "unable to inspect input ZIP: " + fileError.message()
            : "input path is not a regular file";
        return result;
    }
    if (asciiLower(archivePath.extension().u8string()) != ".zip")
    {
        result.archiveError = "input file must have a .zip extension";
        return result;
    }

    ZipArchive archive;
    if (!archive.open(archivePath))
    {
        result.archiveError = "unable to open ZIP archive: " + archive.lastError();
        return result;
    }

    result.outputDirectory = archivePath.parent_path() / archivePath.stem();
    std::filesystem::create_directories(result.outputDirectory, fileError);
    if (fileError)
    {
        result.archiveError = "unable to create output directory: " + fileError.message();
        return result;
    }

    std::set<std::wstring> outputPaths;
    for (const ZipEntryInfo& entry : archive.entries())
    {
        enum class Kind { None, Hiklog, Rbt, Dat };
        Kind kind = Kind::None;
        const char* expectedTable = nullptr;
        const char* extension = nullptr;
        if (isHiklog(entry))
        {
            kind = Kind::Hiklog;
            extension = ".csv";
            ++result.hiklogCount;
        }
        else if (isRbtLog(entry))
        {
            kind = Kind::Rbt;
            extension = ".txt";
            ++result.rbtCount;
        }
        else if ((expectedTable = expectedDatTable(entry)) != nullptr)
        {
            kind = Kind::Dat;
            extension = ".json";
            ++result.datCount;
        }
        if (kind == Kind::None)
            continue;

        const std::filesystem::path outputPath =
            outputPathFor(result.outputDirectory, entry, extension);
        if (!entry.canRead())
        {
            result.items.push_back({kind == Kind::Hiklog ? "HikLog"
                                       : kind == Kind::Rbt ? "RBT" : "DAT",
                                    entry.pathUtf8, outputPath, false,
                                    "ZIP entry is unsafe, encrypted, or unsupported"});
            continue;
        }
        if (!outputPaths.insert(pathKey(outputPath)).second)
        {
            result.items.push_back({kind == Kind::Hiklog ? "HikLog"
                                       : kind == Kind::Rbt ? "RBT" : "DAT",
                                    entry.pathUtf8, outputPath, false,
                                    "multiple ZIP entries map to the same output path"});
            continue;
        }

        ItemResult item;
        switch (kind)
        {
        case Kind::Hiklog:
            item = convertHiklog(archive, entry, outputPath);
            if (item.success)
                ++result.hiklogSuccess;
            break;
        case Kind::Rbt:
            item = convertRbt(archive, entry, outputPath);
            if (item.success)
                ++result.rbtSuccess;
            break;
        case Kind::Dat:
            item = convertDat(archive, entry, outputPath, expectedTable);
            if (item.success)
                ++result.datSuccess;
            break;
        case Kind::None:
            break;
        }
        result.items.push_back(std::move(item));
    }

    if (result.items.empty())
        result.archiveError = "ZIP archive contains no supported log or DAT entries";
    return result;
}

} // namespace quickaction
