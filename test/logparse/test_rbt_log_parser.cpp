#include "test_case.h"

#include "code_logparse/rbt_log_parser.h"
#include "code_logparse/ziplog/zip_archive.h"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{

using Bytes = std::vector<uint8_t>;

void appendU32(Bytes& bytes, uint32_t value)
{
    for (size_t byte = 0; byte < 4; ++byte)
        bytes.push_back(static_cast<uint8_t>(value >> (byte * 8)));
}

void appendRbtBlock(Bytes& bytes, const std::string& text, uint32_t reserved)
{
    uLongf compressedSize = compressBound(static_cast<uLong>(text.size()));
    Bytes compressed(static_cast<size_t>(compressedSize));
    const int status = compress2(
        compressed.data(), &compressedSize,
        reinterpret_cast<const Bytef*>(text.data()),
        static_cast<uLong>(text.size()), Z_BEST_SPEED);
    if (status != Z_OK)
        throw test::TestFailure("unable to create the RBT zlib fixture");
    compressed.resize(static_cast<size_t>(compressedSize));

    bytes.insert(bytes.end(), {'$', 'A', 'G', 'V'});
    appendU32(bytes, reserved);
    appendU32(bytes, static_cast<uint32_t>(compressed.size()));
    bytes.insert(bytes.end(), compressed.begin(), compressed.end());
}

std::filesystem::path rbtTestDirectory()
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() / "csv_viewer_rbt_log_tests";
    std::filesystem::create_directories(directory);
    return directory;
}

std::filesystem::path writeBytes(const std::string& name, const Bytes& bytes)
{
    const std::filesystem::path path = rbtTestDirectory() / name;
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return path;
}

std::string readText(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

} // namespace

TEST_GROUP(RbtLogParser)
{

TEST(RbtLogParser, InflatesEveryBlockAndNormalizesCrLfAcrossBlocks)
{
    Bytes inputBytes;
    appendRbtBlock(inputBytes, "first\r", 0x12345678u);
    appendRbtBlock(inputBytes, "\nsecond\r\nthird\n", 0xffffffffu);
    const auto inputPath = writeBytes("multiple_blocks.rbt", inputBytes);
    const auto outputPath = rbtTestDirectory() / "multiple_blocks.log";

    viewer::logparse::LocalFileBinaryInput input(inputPath);
    viewer::logparse::RbtLogParser parser;
    const auto result = parser.parse(input, outputPath);

    TEST_ASSERT_TRUE(result.success());
    TEST_ASSERT_EQ(result.blockCount, 2u);
    TEST_ASSERT_EQ(readText(outputPath), std::string("first\n\nsecond\n\nthird\n"));
    TEST_ASSERT_EQ(result.outputBytes, readText(outputPath).size());
}

TEST(RbtLogParser, RejectsInvalidMagicAndRemovesPartialOutput)
{
    Bytes inputBytes;
    appendRbtBlock(inputBytes, "valid first block\r\n", 0);
    inputBytes.insert(inputBytes.end(), {'B', 'A', 'D', '!'});
    inputBytes.insert(inputBytes.end(), 8, 0);
    const auto inputPath = writeBytes("invalid_magic.rbt", inputBytes);
    const auto outputPath = rbtTestDirectory() / "invalid_magic.log";

    viewer::logparse::LocalFileBinaryInput input(inputPath);
    viewer::logparse::RbtLogParser parser;
    const auto result = parser.parse(input, outputPath);

    TEST_ASSERT_FALSE(result.success());
    TEST_ASSERT_FALSE(std::filesystem::exists(outputPath));
}

TEST(RbtLogParser, ParsesRepositoryRbtEntryDirectlyFromZip)
{
    const std::filesystem::path zipPath =
        std::filesystem::path(TEST_PROJECT_ROOT) / "data" / "Dev_Log_20260502_185203.zip";
    if (!std::filesystem::exists(zipPath))
        return;

    viewer::logparse::ziplog::ZipArchive archive;
    TEST_ASSERT_TRUE(archive.open(zipPath));
    std::vector<uint64_t> selected;
    for (const auto& entry : archive.entries())
    {
        const std::filesystem::path path = std::filesystem::u8path(entry.pathUtf8);
        const std::string name = path.filename().u8string();
        if (entry.canRead() && name.rfind("RBT", 0) == 0)
        {
            selected.push_back(entry.index);
            break;
        }
    }
    TEST_ASSERT_FALSE(selected.empty());

    const std::filesystem::path outputDirectory = rbtTestDirectory() / "sample_output";
    std::error_code ignored;
    std::filesystem::remove_all(outputDirectory, ignored);
    viewer::logparse::RbtLogParser parser;
    const auto result = parser.parseZipEntries(
        zipPath, selected, outputDirectory);

    TEST_ASSERT_TRUE(result.archiveError.empty());
    TEST_ASSERT_EQ(result.successCount(), 1u);
    TEST_ASSERT_EQ(result.files.size(), 1u);
    TEST_ASSERT_TRUE(result.files[0].blockCount > 0);
    TEST_ASSERT_TRUE(result.files[0].outputBytes > 0);
    TEST_ASSERT_TRUE(std::filesystem::exists(result.files[0].outputPath));
}

} // TEST_GROUP(RbtLogParser)
