#include "test_case.h"
#include "code_viewer/datamgr/data_struct.hpp"

TEST_GROUP(ColumnMisc)
{

// ---------- size tracking across operations ----------
TEST(ColumnMisc, SizeAfterPushBack)
{
    viewer::Column<int64_t> col;
    TEST_ASSERT_EQ(col.size(), 0u);
    col.push_back(1);
    TEST_ASSERT_EQ(col.size(), 1u);
    col.push_back(2);
    TEST_ASSERT_EQ(col.size(), 2u);
    col.push_back(3);
    TEST_ASSERT_EQ(col.size(), 3u);
}

TEST(ColumnMisc, SizeAfterClear)
{
    viewer::Column<int64_t> col;
    col.push_back(10);
    col.push_back(20);
    col.clear();
    TEST_ASSERT_EQ(col.size(), 0u);
}

TEST(ColumnMisc, EmptyAfterPushBack)
{
    viewer::Column<double> col;
    TEST_ASSERT_TRUE(col.empty());
    col.push_back(1.0);
    TEST_ASSERT_FALSE(col.empty());
}

// ---------- operator[] bounds (no bounds check in original, just verify access) ----------
TEST(ColumnMisc, BracketSameAsIndex)
{
    viewer::Column<int64_t> col;
    col.push_back(100);
    col.push_back(200);
    col.push_back(300);
    TEST_ASSERT_EQ(col[0], 100);
    TEST_ASSERT_EQ(col[1], 200);
    TEST_ASSERT_EQ(col[2], 300);
}

// ---------- back() on multi-chunk ----------
TEST(ColumnMisc, BackAfterCrossChunk)
{
    viewer::Column<int64_t> col;
    size_t n = viewer::__chunk_size + 1;
    for (size_t i = 0; i < n; ++i)
        col.push_back(static_cast<int64_t>(i));
    TEST_ASSERT_EQ(col.back(), static_cast<int64_t>(n - 1));
}

// ---------- getDouble/getInt64 outside range are UB, we test valid indices only ----------
TEST(ColumnMisc, GetDoubleValidRange)
{
    viewer::Column<double> col;
    col.push_back(0.0);
    col.push_back(1.0);
    col.push_back(2.0);
    TEST_ASSERT_NEAR(col.getDouble(0), 0.0, 1e-12);
    TEST_ASSERT_NEAR(col.getDouble(1), 1.0, 1e-12);
    TEST_ASSERT_NEAR(col.getDouble(2), 2.0, 1e-12);
}

TEST(ColumnMisc, GetInt64ValidRange)
{
    viewer::Column<int64_t> col;
    col.push_back(-100);
    col.push_back(0);
    col.push_back(100);
    TEST_ASSERT_EQ(col.getInt64(0), -100);
    TEST_ASSERT_EQ(col.getInt64(1), 0);
    TEST_ASSERT_EQ(col.getInt64(2), 100);
}

// ---------- long run push_back ----------
TEST(ColumnMisc, LargePushBack)
{
    viewer::Column<int64_t> col;
    const size_t big = viewer::__chunk_size * 5;
    for (size_t i = 0; i < big; ++i)
        col.push_back(static_cast<int64_t>(i & 0x7FFFFFFF));
    TEST_ASSERT_EQ(col.size(), big);
    TEST_ASSERT_EQ(col[0], 0);
    TEST_ASSERT_EQ(col[big - 1], static_cast<int64_t>((big - 1) & 0x7FFFFFFF));
}

// ---------- type consistency ----------
TEST(ColumnMisc, TypeConsistencyInt)
{
    viewer::Column<int64_t> col;
    TEST_ASSERT_EQ(col.type(), viewer::ColumnType::Int64);
    col.push_back(42);
    TEST_ASSERT_EQ(col.type(), viewer::ColumnType::Int64);
    col.clear();
    TEST_ASSERT_EQ(col.type(), viewer::ColumnType::Int64);
}

TEST(ColumnMisc, TypeConsistencyFloat)
{
    viewer::Column<double> col;
    TEST_ASSERT_EQ(col.type(), viewer::ColumnType::Float64);
    col.push_back(3.14);
    TEST_ASSERT_EQ(col.type(), viewer::ColumnType::Float64);
}

} // TEST_GROUP(ColumnMisc)