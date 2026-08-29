#include "test_case.h"
#include "code_viewer/datamgr/data_manager.h"

#include <filesystem>
#include <fstream>

namespace
{
std::filesystem::path writeCsv(const char* name, const char* contents)
{
    const auto directory = std::filesystem::temp_directory_path()
        / "csv_viewer_datamgr_tests";
    std::filesystem::create_directories(directory);
    const auto path = directory / name;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << contents;
    return path;
}
}

TEST_GROUP(StringColumns)
{

TEST(StringColumns, StoresCsvStringsSeparatelyAndAppendsBySourceSchema)
{
    const auto first = writeCsv(
        "strings_first.csv",
        "time,date,state,value\n"
        "0,2026_08_28_10_11_12,ready,1.5\n"
        "1,2026_08_28_10_11_13,running,2.5\n");
    const auto second = writeCsv(
        "strings_second.csv",
        "time,date,state,value\n"
        "2,2026_08_28_10_11_14,done,3.5\n");

    viewer::DataManager manager;
    manager.SetDateAxisName("date");
    viewer::LoadConfig config;
    config.filePath = first;
    config.preSanitizedNames = {"time", "date", "state", "value"};
    TEST_ASSERT_TRUE(manager.LoadFromCSV(config));
    TEST_ASSERT_EQ(manager.GetColumnCount(), 2u);
    TEST_ASSERT_EQ(manager.GetStringColumnCount(), 2u);
    TEST_ASSERT_EQ(manager.GetRowCount(), 2u);
    TEST_ASSERT_EQ(manager.GetDateValue(1), std::string("2026_08_28_10_11_13"));
    TEST_ASSERT_EQ(manager.GetStringValue("state", 0), std::string("ready"));
    TEST_ASSERT_TRUE(manager.GetColumn("date") == nullptr);

    config.filePath = second;
    TEST_ASSERT_TRUE(manager.LoadFromCSV(config));
    TEST_ASSERT_EQ(manager.GetRowCount(), 3u);
    TEST_ASSERT_EQ(manager.GetStringValue("state", 2), std::string("done"));
    TEST_ASSERT_EQ(manager.GetValueAsDouble("value", 2), 3.5);
}

TEST(StringColumns, ImportsDecodedStringBuffersWithoutPlotColumns)
{
    viewer::DataManager manager;
    manager.SetDateAxisName("date");
    TEST_ASSERT_TRUE(manager.LoadFromColumns(
        {"time"}, {{10.0, 20.0}},
        {"date"}, {{"2026_08_28_10_00_00", "2026_08_28_10_00_01"}},
        "memory"));
    TEST_ASSERT_EQ(manager.GetColumnCount(), 1u);
    TEST_ASSERT_EQ(manager.GetStringColumnCount(), 1u);
    TEST_ASSERT_EQ(manager.GetDateValue(0), std::string("2026_08_28_10_00_00"));
}

TEST(StringColumns, ConfiguredDateAxisIsStringEvenWhenCellsLookNumeric)
{
    const auto path = writeCsv(
        "numeric_date.csv",
        "time,date_code\n"
        "0,20260828101112\n"
        "1,20260828101113\n");
    viewer::DataManager manager;
    manager.SetDateAxisName("date_code");
    viewer::LoadConfig config;
    config.filePath = path;
    config.preSanitizedNames = {"time", "date_code"};
    TEST_ASSERT_TRUE(manager.LoadFromCSV(config));
    TEST_ASSERT_EQ(manager.GetColumnCount(), 1u);
    TEST_ASSERT_EQ(manager.GetStringColumnCount(), 1u);
    TEST_ASSERT_EQ(manager.GetDateValue(0), std::string("20260828101112"));
}

}
