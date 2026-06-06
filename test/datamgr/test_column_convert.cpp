#include "test_case.h"
#include "code_viewer/datamgr/data_struct.hpp"
#include <memory>
#include <cstring>

TEST_GROUP(ColumnConvert)
{

// ---------- copyToDoubleColumn: double → double ----------
TEST(ColumnConvert, DoubleToDoubleSmall)
{
    viewer::Column<double> src;
    src.push_back(1.5);
    src.push_back(2.5);
    src.push_back(3.5);

    viewer::Column<double> dst;
    src.copyToDoubleColumn(&dst);
    TEST_ASSERT_EQ(dst.size(), 3u);
    TEST_ASSERT_NEAR(dst[0], 1.5, 1e-12);
    TEST_ASSERT_NEAR(dst[1], 2.5, 1e-12);
    TEST_ASSERT_NEAR(dst[2], 3.5, 1e-12);
}

TEST(ColumnConvert, DoubleToDoubleEmptySrc)
{
    viewer::Column<double> src;
    viewer::Column<double> dst;
    src.copyToDoubleColumn(&dst);
    TEST_ASSERT_TRUE(dst.empty());
    TEST_ASSERT_EQ(dst.size(), 0u);
}

TEST(ColumnConvert, DoubleToDoubleCrossChunk)
{
    viewer::Column<double> src;
    size_t n = viewer::__chunk_size + 50;
    for (size_t i = 0; i < n; ++i)
        src.push_back(static_cast<double>(i));

    viewer::Column<double> dst;
    src.copyToDoubleColumn(&dst);
    TEST_ASSERT_EQ(dst.size(), n);
    for (size_t i = 0; i < n; ++i)
        TEST_ASSERT_NEAR(dst[i], static_cast<double>(i), 1e-12);
}

TEST(ColumnConvert, DoubleToDoubleAppend)
{
    // 测试追加语义：dst 非空时追加
    viewer::Column<double> dst;
    dst.push_back(100.0);

    viewer::Column<double> src;
    src.push_back(1.0);
    src.push_back(2.0);
    src.copyToDoubleColumn(&dst);

    TEST_ASSERT_EQ(dst.size(), 3u);
    TEST_ASSERT_NEAR(dst[0], 100.0, 1e-12);
    TEST_ASSERT_NEAR(dst[1], 1.0, 1e-12);
    TEST_ASSERT_NEAR(dst[2], 2.0, 1e-12);
}

// ---------- copyToDoubleColumn: int64_t → double ----------
TEST(ColumnConvert, Int64ToDoubleSmall)
{
    viewer::Column<int64_t> src;
    src.push_back(1);
    src.push_back(-2);
    src.push_back(100);

    viewer::Column<double> dst;
    src.copyToDoubleColumn(&dst);
    TEST_ASSERT_EQ(dst.size(), 3u);
    TEST_ASSERT_NEAR(dst[0], 1.0, 1e-12);
    TEST_ASSERT_NEAR(dst[1], -2.0, 1e-12);
    TEST_ASSERT_NEAR(dst[2], 100.0, 1e-12);
}

TEST(ColumnConvert, Int64ToDoubleCrossChunk)
{
    viewer::Column<int64_t> src;
    size_t n = viewer::__chunk_size + 42;
    for (size_t i = 0; i < n; ++i)
        src.push_back(static_cast<int64_t>(i * 3));

    viewer::Column<double> dst;
    src.copyToDoubleColumn(&dst);
    TEST_ASSERT_EQ(dst.size(), n);
    for (size_t i = 0; i < n; ++i)
        TEST_ASSERT_NEAR(dst[i], static_cast<double>(i * 3), 1e-12);
}

TEST(ColumnConvert, Int64ToDoubleEmptySrc)
{
    viewer::Column<int64_t> src;
    viewer::Column<double> dst;
    src.copyToDoubleColumn(&dst);
    TEST_ASSERT_TRUE(dst.empty());
}

// ---------- via AbstractColumn interface ----------
TEST(ColumnConvert, AbstractCopyDouble)
{
    viewer::Column<double> src;
    src.push_back(6.0);
    src.push_back(7.0);

    viewer::Column<double> dst;
    viewer::AbstractColumn& a = src;
    a.copyToDoubleColumn(&dst);
    TEST_ASSERT_EQ(dst.size(), 2u);
    TEST_ASSERT_NEAR(dst[0], 6.0, 1e-12);
    TEST_ASSERT_NEAR(dst[1], 7.0, 1e-12);
}

TEST(ColumnConvert, AbstractCopyInt64)
{
    viewer::Column<int64_t> src;
    src.push_back(42);
    src.push_back(-99);

    viewer::Column<double> dst;
    viewer::AbstractColumn& a = src;
    a.copyToDoubleColumn(&dst);
    TEST_ASSERT_EQ(dst.size(), 2u);
    TEST_ASSERT_NEAR(dst[0], 42.0, 1e-12);
    TEST_ASSERT_NEAR(dst[1], -99.0, 1e-12);
}

// ---------- large scale correctness ----------
TEST(ColumnConvert, LargeScaleDoubleToDouble)
{
    viewer::Column<double> src;
    const size_t n = viewer::__chunk_size * 3 + 123;
    for (size_t i = 0; i < n; ++i)
        src.push_back(static_cast<double>(i) * 1.5);

    viewer::Column<double> dst;
    src.copyToDoubleColumn(&dst);
    TEST_ASSERT_EQ(dst.size(), n);
    for (size_t i = 0; i < 10; ++i)
        TEST_ASSERT_NEAR(dst[i], static_cast<double>(i) * 1.5, 1e-12);
    for (size_t i = n - 10; i < n; ++i)
        TEST_ASSERT_NEAR(dst[i], static_cast<double>(i) * 1.5, 1e-12);
}

} // TEST_GROUP(ColumnConvert)