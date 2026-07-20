#include "test_case.h"

#include "code_logparse/binary_log_parser.h"

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

} // namespace

TEST_GROUP(BinaryLogParser)
{

TEST(BinaryLogParser, ExpandsNumericArrayButKeepsOneStringColumn)
{
    const auto file = writeFixture("primary.hiklog", makePrimaryFile());
    viewer::logparse::BinaryLogParser parser;
    const auto result = parser.parseFiles({file});

    TEST_ASSERT_TRUE(result.success());
    TEST_ASSERT_EQ(result.timestampCount, 2u);
    TEST_ASSERT_EQ(result.columns.size(), 6u);

    const auto* timestamp = findColumn(result, "timestamp");
    const auto* x = findColumn(result, "packet_position_0");
    const auto* y = findColumn(result, "packet_position_1");
    const auto* label = findColumn(result, "packet_label");
    const auto* count = findColumn(result, "packet_count");
    const auto* date = findColumn(result, "meta_date");
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
    TEST_ASSERT_TRUE(std::isnan(label->values[0]));
    TEST_ASSERT_TRUE(std::isnan(label->values[1]));
    TEST_ASSERT_EQ(date->values[0], 0.0);
    TEST_ASSERT_TRUE(std::isnan(date->values[1]));
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
    TEST_ASSERT_EQ(result.columns.size(), 6u);
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

TEST(BinaryLogParser, ReportsTruncatedKnownPayload)
{
    Bytes bytes;
    appendFileHeader(bytes);
    appendTypeDefinition(bytes, 2, 8, "", "Q", {"timestamp"});
    appendTypeDefinition(bytes, 900, 4, "data", "I", {"value"});
    appendFrameHeader(bytes, 900);
    appendU16(bytes, 12);
    const auto file = writeFixture("truncated_payload.hiklog", bytes);

    viewer::logparse::BinaryLogParser parser;
    const auto result = parser.parseFiles({file});
    TEST_ASSERT_FALSE(result.success());
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

} // TEST_GROUP(BinaryLogParser)
