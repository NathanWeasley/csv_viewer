#include "test_case.h"
#include "code_viewer/datamgr/data_struct.hpp"
#include <cmath>
#include <limits>

TEST_GROUP(ClassifyCell)
{

TEST(ClassifyCell, EmptyString)
{
    TEST_ASSERT_EQ(viewer::classifyCell(""), viewer::CellType::Float);
}

TEST(ClassifyCell, PositiveInt)
{
    TEST_ASSERT_EQ(viewer::classifyCell("42"), viewer::CellType::Int);
}

TEST(ClassifyCell, NegativeInt)
{
    TEST_ASSERT_EQ(viewer::classifyCell("-100"), viewer::CellType::Int);
}

TEST(ClassifyCell, Zero)
{
    TEST_ASSERT_EQ(viewer::classifyCell("0"), viewer::CellType::Int);
}

TEST(ClassifyCell, FloatWithDot)
{
    TEST_ASSERT_EQ(viewer::classifyCell("3.14"), viewer::CellType::Float);
}

TEST(ClassifyCell, FloatScientific)
{
    TEST_ASSERT_EQ(viewer::classifyCell("1.5e10"), viewer::CellType::Float);
}

TEST(ClassifyCell, NegativeFloat)
{
    TEST_ASSERT_EQ(viewer::classifyCell("-2.718"), viewer::CellType::Float);
}

TEST(ClassifyCell, LeadingZeroInt)
{
    TEST_ASSERT_EQ(viewer::classifyCell("007"), viewer::CellType::Int);
}

TEST(ClassifyCell, PlainString)
{
    TEST_ASSERT_EQ(viewer::classifyCell("hello"), viewer::CellType::String);
}

TEST(ClassifyCell, AlphaNumeric)
{
    TEST_ASSERT_EQ(viewer::classifyCell("123abc"), viewer::CellType::String);
}

TEST(ClassifyCell, WhitespaceOnly)
{
    TEST_ASSERT_EQ(viewer::classifyCell("   "), viewer::CellType::String);
}

TEST(ClassifyCell, LeadingSpacesNumber)
{
    // strtoll skips leading whitespace, so "  42" is parsed as int
    TEST_ASSERT_EQ(viewer::classifyCell("  42"), viewer::CellType::Int);
}

} // TEST_GROUP(ClassifyCell)