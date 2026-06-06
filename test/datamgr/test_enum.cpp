#include "test_case.h"
#include "code_viewer/datamgr/data_struct.hpp"

TEST_GROUP(Enum)
{

TEST(Enum, CellTypeValues)
{
    TEST_ASSERT_EQ(static_cast<uint8_t>(viewer::CellType::Int),    0);
    TEST_ASSERT_EQ(static_cast<uint8_t>(viewer::CellType::Float),  1);
    TEST_ASSERT_EQ(static_cast<uint8_t>(viewer::CellType::String), 2);
}

TEST(Enum, ColumnTypeValues)
{
    TEST_ASSERT_EQ(static_cast<uint8_t>(viewer::ColumnType::Unknown), 0);
    TEST_ASSERT_EQ(static_cast<uint8_t>(viewer::ColumnType::Int64),   1);
    TEST_ASSERT_EQ(static_cast<uint8_t>(viewer::ColumnType::Float64), 2);
}

TEST(Enum, ColumnTypeDistinct)
{
    TEST_ASSERT_NE(viewer::ColumnType::Unknown, viewer::ColumnType::Int64);
    TEST_ASSERT_NE(viewer::ColumnType::Unknown, viewer::ColumnType::Float64);
    TEST_ASSERT_NE(viewer::ColumnType::Int64,   viewer::ColumnType::Float64);
}

} // TEST_GROUP(Enum)