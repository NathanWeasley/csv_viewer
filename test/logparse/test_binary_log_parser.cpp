#include "test_case.h"

#include "code_logparse/binary_log_parser.h"
#include "code_logparse/ziplog/zip_archive.h"
#include "code_viewer/datamgr/data_struct.hpp"

#include <zip.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <string>
#include <vector>

namespace
{

using Bytes = std::vector<uint8_t>;

void appendU16(Bytes& bytes, uint16_t value)
{
    bytes.push_back(static_cast<uint8_t>(value));
    bytes.push_back(static_cast<uint8_t>(value >> 8));
}

void appendU32(Bytes& bytes, uint32_t value)
{
    for (size_t i = 0; i < 4; ++i)
        bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
}

void appendU64(Bytes& bytes, uint64_t value)
{
    for (size_t i = 0; i < 8; ++i)
        bytes.push_back(static_cast<uint8_t>(value >> (i * 8)));
}

void appendFloat(Bytes& bytes, float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    appendU32(bytes, bits);
}

void appendString(Bytes& bytes, const std::string& value)
{
    bytes.insert(bytes.end(), value.begin(), value.end());
    bytes.push_back(0);
}

void appendFileHeader(Bytes& bytes)
{
    bytes.push_back(0x90);
    bytes.push_back(0xEB);
    bytes.insert(bytes.end(), 10, 0);
    appendU32(bytes, 0);
}

void appendFrameHeader(Bytes& bytes, uint16_t frameType)
{
    bytes.push_back(0xA5);
    bytes.push_back(0x5A);
    appendU16(bytes, frameType);
}

void appendTypeDefinition(Bytes& bytes,
                          uint32_t id,
                          uint32_t payloadLength,
                          const std::string& packetName,
                          const std::string& format,
                          const std::vector<std::string>& fieldNames)
{
    Bytes packetNameBlock;
    Bytes formatBlock;
    Bytes fieldNamesBlock;
    appendString(packetNameBlock, packetName);
    appendString(formatBlock, format);
    for (const auto& name : fieldNames)
        appendString(fieldNamesBlock, name);

    appendFrameHeader(bytes, 1);
    appendU32(bytes, id);
    appendU32(bytes, payloadLength);
    appendU32(bytes, static_cast<uint32_t>(packetNameBlock.size()));
    appendU32(bytes, static_cast<uint32_t>(formatBlock.size()));
    appendU32(bytes, static_cast<uint32_t>(fieldNamesBlock.size()));
    bytes.insert(bytes.end(), packetNameBlock.begin(), packetNameBlock.end());
    bytes.insert(bytes.end(), formatBlock.begin(), formatBlock.end());
    bytes.insert(bytes.end(), fieldNamesBlock.begin(), fieldNamesBlock.end());
}

void appendCommaSeparatedTypeDefinition(Bytes& bytes,
                                        uint32_t id,
                                        uint32_t payloadLength,
                                        const std::string& packetName,
                                        const std::string& format,
                                        const std::string& fieldNames)
{
    Bytes packetNameBlock;
    Bytes formatBlock;
    Bytes fieldNamesBlock;
    appendString(packetNameBlock, packetName);
    appendString(formatBlock, format);
    appendString(fieldNamesBlock, fieldNames);

    appendFrameHeader(bytes, 1);
    appendU32(bytes, id);
    appendU32(bytes, payloadLength);
    appendU32(bytes, static_cast<uint32_t>(packetNameBlock.size()));
    appendU32(bytes, static_cast<uint32_t>(formatBlock.size()));
    appendU32(bytes, static_cast<uint32_t>(fieldNamesBlock.size()));
    bytes.insert(bytes.end(), packetNameBlock.begin(), packetNameBlock.end());
    bytes.insert(bytes.end(), formatBlock.begin(), formatBlock.end());
    bytes.insert(bytes.end(), fieldNamesBlock.begin(), fieldNamesBlock.end());
}

void appendTimestamp(Bytes& bytes, uint64_t timestamp)
{
    appendFrameHeader(bytes, 2);
    appendU64(bytes, timestamp);
}

void appendDate(Bytes& bytes, uint8_t fill)
{
    appendFrameHeader(bytes, 3);
    bytes.insert(bytes.end(), 64, fill);
}

void appendPacket900(Bytes& bytes, float x, float y, uint16_t count)
{
    appendFrameHeader(bytes, 900);
    appendFloat(bytes, x);
    appendFloat(bytes, y);
    const char text[5] = {'a', 'b', 'c', 'd', '\0'};
    bytes.insert(bytes.end(), text, text + 5);
    appendU16(bytes, count);
}

std::filesystem::path writeFixture(const std::string& name, const Bytes& bytes)
{
    const auto directory = std::filesystem::temp_directory_path()
        / "csv_viewer_logparse_tests";
    std::filesystem::create_directories(directory);
    const auto path = directory / name;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.close();
    return path;
}

const viewer::logparse::ParsedColumn* findColumn(
    const viewer::logparse::ParseResult& result, const std::string& name)
{
    for (const auto& column : result.columns)
    {
        if (column.name == name)
            return &column;
    }
    return nullptr;
}

const viewer::logparse::ParsedStringColumn* findStringColumn(
    const viewer::logparse::ParseResult& result, const std::string& name)
{
    for (const auto& column : result.stringColumns)
    {
        if (column.name == name)
            return &column;
    }
    return nullptr;
}

const viewer::logparse::ParsedColumn* findPacketColumn(
    const viewer::logparse::ParseResult& result, uint32_t packetId)
{
    for (const auto& column : result.columns)
    {
        if (column.packetId == packetId)
            return &column;
    }
    return nullptr;
}

void writeCsvCell(std::ostream& output, const std::string& text)
{
    const bool needsQuotes = text.find_first_of(",\"\r\n") != std::string::npos;
    if (!needsQuotes)
    {
        output << text;
        return;
    }

    output.put('"');
    for (const char c : text)
    {
        if (c == '"')
            output.put('"');
        output.put(c);
    }
    output.put('"');
}

void writeParsedCsv(const viewer::logparse::ParseResult& result,
                    const std::filesystem::path& outputPath)
{
    std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
    if (!output.is_open())
        throw test::TestFailure("failed to create parsed CSV: " + outputPath.string());

    output.imbue(std::locale::classic());
    output << std::setprecision(std::numeric_limits<double>::max_digits10);

    for (size_t column = 0; column < result.columns.size(); ++column)
    {
        if (column > 0)
            output.put(',');
        writeCsvCell(output, result.columns[column].name);
    }
    output.put('\n');

    for (size_t row = 0; row < result.timestampCount; ++row)
    {
        for (size_t column = 0; column < result.columns.size(); ++column)
        {
            if (column > 0)
                output.put(',');

            if (row >= result.columns[column].values.size())
                throw test::TestFailure("parsed columns have inconsistent row counts");

            const double value = result.columns[column].values[row];
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
        throw test::TestFailure("failed while writing parsed CSV: " + outputPath.string());
}

Bytes makePrimaryFile()
{
    Bytes bytes;
    appendFileHeader(bytes);
    appendTypeDefinition(bytes, 2, 8, "", "Q", {"timestamp"});
    appendTypeDefinition(bytes, 3, 64, "meta", "Z", {"date"});
    appendTypeDefinition(bytes, 900, 15, "packet !", "f[2]c[5]H",
                         {"position", "label", "count"});

    appendPacket900(bytes, 1.0f, 2.0f, 7);
    appendTimestamp(bytes, 100);
    appendPacket900(bytes, 3.0f, 4.0f, 9);
    appendDate(bytes, '2');
    appendTimestamp(bytes, 200);
    return bytes;
}

std::filesystem::path writeZipFixture(
    const std::string& name,
    const std::vector<std::pair<std::string, Bytes>>& files)
{
    const auto directory = std::filesystem::temp_directory_path()
        / "csv_viewer_logparse_tests";
    std::filesystem::create_directories(directory);
    const auto path = directory / name;

    int errorCode = 0;
    zip_t* archive = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &errorCode);
    if (!archive)
        throw test::TestFailure("failed to create ZIP fixture");

    if (zip_dir_add(archive, "nested/", ZIP_FL_ENC_UTF_8) < 0)
    {
        const std::string error = zip_strerror(archive);
        zip_discard(archive);
        throw test::TestFailure("failed to add ZIP directory: " + error);
    }
    for (const auto& file : files)
    {
        zip_source_t* source = zip_source_buffer(
            archive, file.second.data(), file.second.size(), 0);
        if (!source
            || zip_file_add(archive, file.first.c_str(), source,
                            ZIP_FL_ENC_UTF_8 | ZIP_FL_OVERWRITE) < 0)
        {
            if (source)
                zip_source_free(source);
            const std::string error = zip_strerror(archive);
            zip_discard(archive);
            throw test::TestFailure("failed to add ZIP file: " + error);
        }
    }
    if (zip_close(archive) != 0)
    {
        zip_discard(archive);
        throw test::TestFailure("failed to close ZIP fixture");
    }
    return path;
}

} // namespace

TEST_GROUP(BinaryLogParser)
{

TEST(BinaryLogParser, ExpandsNumericArrayAndSeparatesStringColumns)
{
    const auto file = writeFixture("primary.hiklog", makePrimaryFile());
    viewer::logparse::BinaryLogParser parser;
    const auto result = parser.parseFiles({file});

    TEST_ASSERT_TRUE(result.success());
    TEST_ASSERT_EQ(result.timestampCount, 2u);
    TEST_ASSERT_EQ(result.columns.size(), 4u);
    TEST_ASSERT_EQ(result.stringColumns.size(), 2u);

    const auto* timestamp = findColumn(result, "timestamp");
    const auto* x = findColumn(result, "packet_position_0");
    const auto* y = findColumn(result, "packet_position_1");
    const auto* label = findStringColumn(result, "packet_label");
    const auto* count = findColumn(result, "packet_count");
    const auto* date = findStringColumn(result, "meta_date");
    TEST_ASSERT_TRUE(timestamp && x && y && label && count && date);

    TEST_ASSERT_EQ(timestamp->values.size(), 2u);
    TEST_ASSERT_EQ(timestamp->values[0], 100.0);
    TEST_ASSERT_EQ(timestamp->values[1], 200.0);
    TEST_ASSERT_EQ(x->values[0], 1.0);
    TEST_ASSERT_EQ(x->values[1], 3.0);
    TEST_ASSERT_EQ(y->values[0], 2.0);
    TEST_ASSERT_EQ(y->values[1], 4.0);
    TEST_ASSERT_EQ(count->values[0], 7.0);
    TEST_ASSERT_EQ(count->values[1], 9.0);
    TEST_ASSERT_EQ(label->values[0], std::string("abcd"));
    TEST_ASSERT_EQ(label->values[1], std::string("abcd"));
    TEST_ASSERT_TRUE(date->values[0].empty());
    TEST_ASSERT_EQ(date->values[1], std::string(64, '2'));
}

TEST(BinaryLogParser, KeepsMasterSchemaAcrossFilesAndIgnoresAdditionalType)
{
    const auto first = writeFixture("multi_first.hiklog", makePrimaryFile());

    Bytes secondBytes;
    appendFileHeader(secondBytes);
    appendTypeDefinition(secondBytes, 2, 8, "", "Q", {"timestamp"});
    appendTypeDefinition(secondBytes, 900, 15, "packet !", "f[2]c[5]H",
                         {"position", "label", "count"});
    appendTypeDefinition(secondBytes, 901, 4, "extra", "I", {"value"});
    appendPacket900(secondBytes, 5.0f, 6.0f, 11);
    appendFrameHeader(secondBytes, 901);
    appendU32(secondBytes, 1234);
    appendTimestamp(secondBytes, 300);
    const auto second = writeFixture("multi_second.hiklog", secondBytes);

    viewer::logparse::BinaryLogParser parser;
    const auto result = parser.parseFiles({first, second});

    TEST_ASSERT_EQ(result.timestampCount, 3u);
    TEST_ASSERT_EQ(result.fileRanges.size(), 2u);
    TEST_ASSERT_EQ(result.fileRanges[0].filePath, first);
    TEST_ASSERT_EQ(result.fileRanges[0].firstRow, 0u);
    TEST_ASSERT_EQ(result.fileRanges[0].rowCount, 2u);
    TEST_ASSERT_EQ(result.fileRanges[1].filePath, second);
    TEST_ASSERT_EQ(result.fileRanges[1].firstRow, 2u);
    TEST_ASSERT_EQ(result.fileRanges[1].rowCount, 1u);
    TEST_ASSERT_EQ(result.columns.size(), 4u);
    TEST_ASSERT_EQ(result.stringColumns.size(), 2u);
    TEST_ASSERT_TRUE(findColumn(result, "extra_value") == nullptr);
    const auto* x = findColumn(result, "packet_position_0");
    const auto* timestamp = findColumn(result, "timestamp");
    TEST_ASSERT_TRUE(x && timestamp);
    TEST_ASSERT_EQ(x->values.size(), 3u);
    TEST_ASSERT_EQ(x->values[2], 5.0);
    TEST_ASSERT_EQ(timestamp->values[2], 300.0);
}

TEST(BinaryLogParser, ResynchronizesAfterUnknownPacket)
{
    Bytes bytes;
    appendFileHeader(bytes);
    appendTypeDefinition(bytes, 2, 8, "", "Q", {"timestamp"});
    appendFrameHeader(bytes, 950);
    bytes.insert(bytes.end(), 13, 0x44);
    appendTimestamp(bytes, 42);
    const auto file = writeFixture("unknown_packet.hiklog", bytes);

    viewer::logparse::BinaryLogParser parser;
    const auto result = parser.parseFiles({file});
    const auto* timestamp = findColumn(result, "timestamp");
    TEST_ASSERT_TRUE(timestamp != nullptr);
    TEST_ASSERT_EQ(result.timestampCount, 1u);
    TEST_ASSERT_EQ(timestamp->values.size(), 1u);
    TEST_ASSERT_EQ(timestamp->values[0], 42.0);
}

TEST(BinaryLogParser, AcceptsCommaSeparatedFieldNameBlock)
{
    Bytes bytes;
    appendFileHeader(bytes);
    appendCommaSeparatedTypeDefinition(bytes, 2, 8, "time", "Q", "ts");
    appendCommaSeparatedTypeDefinition(bytes, 900, 16, "dc", "Qii",
                                       "wakeup_time, dc_diff_ns, system_jitter");
    appendFrameHeader(bytes, 900);
    appendU64(bytes, 10);
    appendU32(bytes, 20);
    appendU32(bytes, 30);
    appendTimestamp(bytes, 100);
    const auto file = writeFixture("comma_names.hiklog", bytes);

    viewer::logparse::BinaryLogParser parser;
    const auto result = parser.parseFiles({file});
    TEST_ASSERT_TRUE(result.success());
    TEST_ASSERT_TRUE(findColumn(result, "dc_wakeup_time") != nullptr);
    TEST_ASSERT_TRUE(findColumn(result, "dc_dc_diff_ns") != nullptr);
    TEST_ASSERT_TRUE(findColumn(result, "dc_system_jitter") != nullptr);
}

TEST(BinaryLogParser, ParsesOptionalRepositorySamplesSequentiallyAndExportsCsv)
{
    const std::filesystem::path dataDirectory =
        std::filesystem::path(TEST_PROJECT_ROOT) / "data";
    std::vector<std::filesystem::path> samples;
    for (const auto& entry : std::filesystem::directory_iterator(dataDirectory))
    {
        if (entry.is_regular_file() && entry.path().extension() == ".hiklog")
            samples.push_back(entry.path());
    }
    std::sort(samples.begin(), samples.end());
    if (samples.size() < 2)
        return;

    viewer::logparse::BinaryLogParser parser;
    struct FileBoundary
    {
        size_t rowOffset = 0;
        size_t rowCount = 0;
        double firstTimestamp = 0.0;
        double lastTimestamp = 0.0;
    };
    std::vector<FileBoundary> boundaries;
    size_t expectedRows = 0;
    for (const auto& sample : samples)
    {
        const auto singleResult = parser.parseFiles({sample});
        TEST_ASSERT_TRUE(singleResult.success());
        TEST_ASSERT_TRUE(singleResult.timestampCount > 0);
        const auto* timestamp = findPacketColumn(singleResult, 2);
        TEST_ASSERT_TRUE(timestamp != nullptr);
        TEST_ASSERT_EQ(timestamp->values.size(), singleResult.timestampCount);

        boundaries.push_back({
            expectedRows,
            singleResult.timestampCount,
            timestamp->values.front(),
            timestamp->values.back()
        });
        expectedRows += singleResult.timestampCount;
    }

    const auto result = parser.parseFiles(samples);
    if (!result.success())
    {
        std::string details = "sequential sample parse failed:";
        for (const auto& diagnostic : result.diagnostics)
        {
            if (diagnostic.severity == viewer::logparse::DiagnosticSeverity::Error)
            {
                details += "\n  offset " + std::to_string(diagnostic.byteOffset)
                         + ": " + diagnostic.message;
            }
        }
        throw test::TestFailure(details);
    }

    TEST_ASSERT_TRUE(!result.packetTypes.empty());
    TEST_ASSERT_EQ(result.timestampCount, expectedRows);
    TEST_ASSERT_EQ(result.fileRanges.size(), samples.size());
    for (const auto& column : result.columns)
        TEST_ASSERT_EQ(column.values.size(), result.timestampCount);

    const auto* timestamp = findPacketColumn(result, 2);
    TEST_ASSERT_TRUE(timestamp != nullptr);
    for (size_t index = 0; index < boundaries.size(); ++index)
    {
        const auto& boundary = boundaries[index];
        TEST_ASSERT_EQ(result.fileRanges[index].filePath, samples[index]);
        TEST_ASSERT_EQ(result.fileRanges[index].firstRow, boundary.rowOffset);
        TEST_ASSERT_EQ(result.fileRanges[index].rowCount, boundary.rowCount);
        TEST_ASSERT_EQ(timestamp->values[boundary.rowOffset], boundary.firstTimestamp);
        TEST_ASSERT_EQ(
            timestamp->values[boundary.rowOffset + boundary.rowCount - 1],
            boundary.lastTimestamp);
    }

    const std::filesystem::path csvPath =
        std::filesystem::path(TEST_PROJECT_ROOT) / "data" / "hiklog_parsed.csv";
    writeParsedCsv(result, csvPath);
    TEST_ASSERT_TRUE(std::filesystem::exists(csvPath));
    TEST_ASSERT_TRUE(std::filesystem::file_size(csvPath) > 0);
}

TEST(BinaryLogParser, RejectsCharacterTypeWithoutLength)
{
    Bytes bytes;
    appendFileHeader(bytes);
    appendTypeDefinition(bytes, 900, 1, "text", "c", {"value"});
    const auto file = writeFixture("invalid_character_format.hiklog", bytes);

    viewer::logparse::BinaryLogParser parser;
    const auto result = parser.parseFiles({file});
    TEST_ASSERT_FALSE(result.success());
}

TEST(BinaryLogParser, KeepsCompleteRowsWhenKnownPayloadIsTruncated)
{
    Bytes bytes;
    appendFileHeader(bytes);
    appendTypeDefinition(bytes, 2, 8, "", "Q", {"timestamp"});
    appendTypeDefinition(bytes, 900, 4, "data", "I", {"value"});
    appendFrameHeader(bytes, 900);
    appendU32(bytes, 12);
    appendTimestamp(bytes, 100);
    appendFrameHeader(bytes, 900);
    appendU16(bytes, 34);
    const auto file = writeFixture("truncated_payload.hiklog", bytes);

    viewer::logparse::BinaryLogParser parser;
    const auto result = parser.parseFiles({file});
    TEST_ASSERT_TRUE(result.success());
    TEST_ASSERT_EQ(result.timestampCount, 1u);

    const auto* value = findColumn(result, "data_value");
    TEST_ASSERT_TRUE(value != nullptr);
    TEST_ASSERT_EQ(value->values.size(), 1u);
    TEST_ASSERT_EQ(value->values[0], 12.0);

    bool foundTruncatedWarning = false;
    for (const auto& diagnostic : result.diagnostics)
    {
        if (diagnostic.severity == viewer::logparse::DiagnosticSeverity::Warning
            && diagnostic.message.find("truncated payload for packet ID 900")
                != std::string::npos)
        {
            foundTruncatedWarning = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(foundTruncatedWarning);
}

TEST(BinaryLogParser, IgnoresTypeDefinitionsAfterDataState)
{
    Bytes bytes;
    appendFileHeader(bytes);
    appendTypeDefinition(bytes, 2, 8, "", "Q", {"timestamp"});
    appendTimestamp(bytes, 1);
    appendTypeDefinition(bytes, 900, 4, "late", "I", {"value"});
    appendFrameHeader(bytes, 900);
    appendU32(bytes, 123);
    appendTimestamp(bytes, 2);
    const auto file = writeFixture("late_definition.hiklog", bytes);

    viewer::logparse::BinaryLogParser parser;
    const auto result = parser.parseFiles({file});
    TEST_ASSERT_TRUE(result.success());
    TEST_ASSERT_EQ(result.columns.size(), 1u);
    TEST_ASSERT_TRUE(findColumn(result, "late_value") == nullptr);
    TEST_ASSERT_EQ(result.timestampCount, 2u);
}

TEST(BinaryLogParser, CatalogsAndReadsZipEntriesWithoutExtraction)
{
    const Bytes first = makePrimaryFile();
    Bytes unsafe = {1, 2, 3};
    const auto zipPath = writeZipFixture(
        "binary_inputs.zip",
        {{"nested/first.hiklog", first}, {"../unsafe.hiklog", unsafe}});

    viewer::logparse::ziplog::ZipArchive archive;
    TEST_ASSERT_TRUE(archive.open(zipPath));
    TEST_ASSERT_EQ(archive.entries().size(), 3u);

    const viewer::logparse::ziplog::ZipEntryInfo* logEntry = nullptr;
    const viewer::logparse::ziplog::ZipEntryInfo* unsafeEntry = nullptr;
    for (const auto& entry : archive.entries())
    {
        if (entry.pathUtf8 == "nested/first.hiklog")
            logEntry = &entry;
        if (entry.pathUtf8 == "../unsafe.hiklog")
            unsafeEntry = &entry;
    }
    TEST_ASSERT_TRUE(logEntry != nullptr);
    TEST_ASSERT_TRUE(logEntry->canRead());
    TEST_ASSERT_TRUE(unsafeEntry != nullptr);
    TEST_ASSERT_TRUE(unsafeEntry->hasUnsafePath);
    TEST_ASSERT_FALSE(unsafeEntry->canRead());

    auto input = archive.createInput(logEntry->index);
    TEST_ASSERT_TRUE(input != nullptr);
    TEST_ASSERT_TRUE(input->open());
    TEST_ASSERT_EQ(input->size(), first.size());

    std::vector<uint8_t> head(19);
    TEST_ASSERT_TRUE(input->read(head.data(), head.size()));
    TEST_ASSERT_TRUE(std::equal(head.begin(), head.end(), first.begin()));
    TEST_ASSERT_TRUE(input->seek(2));
    std::vector<uint8_t> middle(31);
    TEST_ASSERT_TRUE(input->read(middle.data(), middle.size()));
    TEST_ASSERT_TRUE(std::equal(middle.begin(), middle.end(), first.begin() + 2));

    TEST_ASSERT_FALSE(std::filesystem::exists(
        zipPath.parent_path() / "nested" / "first.hiklog"));
}

TEST(BinaryLogParser, ParsesMultipleHiklogsDirectlyFromZipInSelectedOrder)
{
    const Bytes first = makePrimaryFile();
    const Bytes second = makePrimaryFile();
    const auto zipPath = writeZipFixture(
        "sequential_inputs.zip",
        {{"nested/second.hiklog", second}, {"nested/first.hiklog", first}});

    viewer::logparse::ziplog::ZipArchive archive;
    TEST_ASSERT_TRUE(archive.open(zipPath));
    uint64_t firstIndex = 0;
    uint64_t secondIndex = 0;
    for (const auto& entry : archive.entries())
    {
        if (entry.pathUtf8 == "nested/first.hiklog")
            firstIndex = entry.index;
        if (entry.pathUtf8 == "nested/second.hiklog")
            secondIndex = entry.index;
    }

    std::vector<std::unique_ptr<viewer::logparse::BinaryInput>> inputs;
    inputs.push_back(archive.createInput(firstIndex));
    inputs.push_back(archive.createInput(secondIndex));
    viewer::logparse::BinaryLogParser parser;
    const auto result = parser.parseInputs(std::move(inputs));
    TEST_ASSERT_TRUE(result.success());
    TEST_ASSERT_EQ(result.timestampCount, 4u);
    TEST_ASSERT_EQ(result.fileRanges.size(), 2u);
    TEST_ASSERT_EQ(result.fileRanges[0].firstRow, 0u);
    TEST_ASSERT_EQ(result.fileRanges[0].rowCount, 2u);
    TEST_ASSERT_EQ(result.fileRanges[1].firstRow, 2u);
    TEST_ASSERT_EQ(result.fileRanges[1].rowCount, 2u);
    TEST_ASSERT_TRUE(result.fileRanges[0].filePath.wstring().find(L"first.hiklog")
                     != std::wstring::npos);
    TEST_ASSERT_TRUE(result.fileRanges[1].filePath.wstring().find(L"second.hiklog")
                     != std::wstring::npos);
}

TEST(BinaryLogParser, ParsesRepositoryServoLogsDirectlyFromZip)
{
    const std::filesystem::path zipPath =
        std::filesystem::path(TEST_PROJECT_ROOT) / "data" / "Dev_Log_20260502_185203.zip";
    if (!std::filesystem::exists(zipPath))
        return;

    viewer::logparse::ziplog::ZipArchive archive;
    TEST_ASSERT_TRUE(archive.open(zipPath));
    std::vector<const viewer::logparse::ziplog::ZipEntryInfo*> servoLogs;
    for (const auto& entry : archive.entries())
    {
        if (entry.canRead()
            && entry.pathUtf8.find("rcd_servo_") != std::string::npos
            && entry.pathUtf8.size() >= 7
            && entry.pathUtf8.substr(entry.pathUtf8.size() - 7) == ".hiklog")
        {
            servoLogs.push_back(&entry);
        }
    }
    std::sort(servoLogs.begin(), servoLogs.end(),
        [](const auto* lhs, const auto* rhs) { return lhs->pathUtf8 < rhs->pathUtf8; });
    if (servoLogs.size() < 2)
        return;

    std::vector<std::unique_ptr<viewer::logparse::BinaryInput>> inputs;
    inputs.push_back(archive.createInput(servoLogs[0]->index));
    inputs.push_back(archive.createInput(servoLogs[1]->index));
    viewer::logparse::BinaryLogParser parser;
    const auto result = parser.parseInputs(std::move(inputs));
    TEST_ASSERT_TRUE(result.success());
    TEST_ASSERT_EQ(result.columns.size() + result.stringColumns.size(), 268u);
    TEST_ASSERT_EQ(result.timestampCount, 115941u);
    TEST_ASSERT_EQ(result.fileRanges.size(), 2u);
    TEST_ASSERT_TRUE(result.fileRanges[0].rowCount > 0);
    TEST_ASSERT_TRUE(result.fileRanges[1].rowCount > 0);
    TEST_ASSERT_EQ(result.timestampCount,
                   result.fileRanges[0].rowCount + result.fileRanges[1].rowCount);
    for (const auto& column : result.columns)
        TEST_ASSERT_EQ(column.values.size(), result.timestampCount);
    for (const auto& column : result.stringColumns)
        TEST_ASSERT_EQ(column.values.size(), result.timestampCount);
    bool hasDateString = false;
    for (const auto& column : result.stringColumns)
    {
        if (column.name.find("date") == std::string::npos)
            continue;
        hasDateString = std::any_of(
            column.values.begin(), column.values.end(),
            [](const std::string& value) { return !value.empty(); });
        if (hasDateString)
            break;
    }
    TEST_ASSERT_TRUE(hasDateString);
}

TEST(BinaryLogParser, ReportsCancellationSeparatelyFromParseFailure)
{
    const auto file = writeFixture("cancelled.hiklog", makePrimaryFile());
    viewer::logparse::ParseOptions options;
    options.isCancelled = []() { return true; };

    viewer::logparse::BinaryLogParser parser;
    const auto result = parser.parseFiles({file}, options);
    TEST_ASSERT_TRUE(result.cancelled);
    TEST_ASSERT_EQ(result.fileRanges.size(), 1u);
}

TEST(BinaryLogParser, ColumnAdoptsDecodedVectorStorageWithoutCopy)
{
    std::vector<double> values = {1.0, 2.0, 3.0, 4.0};
    const double* originalStorage = values.data();
    viewer::Column column(std::move(values));
    TEST_ASSERT_TRUE(column.data() == originalStorage);
    TEST_ASSERT_EQ(column.size(), 4u);
    TEST_ASSERT_EQ(column[2], 3.0);
}

} // TEST_GROUP(BinaryLogParser)
