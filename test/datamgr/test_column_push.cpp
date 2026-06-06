#include "test_case.h"
#include "code_viewer/datamgr/data_struct.hpp"

TEST_GROUP(ColumnPush)
{

// ---------- push_back + operator[] ----------
TEST(ColumnPush, PushBackSingleInt)
{
    viewer::Column<int64_t> col;
    col.push_back(42);
    TEST_ASSERT_EQ(col.size(), 1u);
    TEST_ASSERT_FALSE(col.empty());
    TEST_ASSERT_EQ(col[0], 42);
}

TEST(ColumnPush, PushBackMultipleInt)
{
    viewer::Column<int64_t> col;
    for (int64_t i = 0; i < 100; ++i)
        col.push_back(i);
    TEST_ASSERT_EQ(col.size(), 100u);
    for (int64_t i = 0; i < 100; ++i)
        TEST_ASSERT_EQ(col[static_cast<size_t>(i)], i);
}

TEST(ColumnPush, PushBackMultipleFloat)
{
    viewer::Column<double> col;
    col.push_back(1.5);
    col.push_back(2.5);
    col.push_back(3.5);
    TEST_ASSERT_EQ(col.size(), 3u);
    TEST_ASSERT_NEAR(col[0], 1.5, 1e-12);
    TEST_ASSERT_NEAR(col[1], 2.5, 1e-12);
    TEST_ASSERT_NEAR(col[2], 3.5, 1e-12);
}

TEST(ColumnPush, OperatorBracketModify)
{
    viewer::Column<int64_t> col;
    col.push_back(1);
    col[0] = 99;
    TEST_ASSERT_EQ(col[0], 99);
}

// ---------- cross-chunk boundary ----------
TEST(ColumnPush, CrossChunkBoundary)
{
    viewer::Column<int64_t> col;
    size_t n = viewer::__chunk_size + 10;
    for (size_t i = 0; i < n; ++i)
        col.push_back(static_cast<int64_t>(i * 2));

    TEST_ASSERT_EQ(col.size(), n);
    TEST_ASSERT_EQ(col[0], 0);
    TEST_ASSERT_EQ(col[viewer::__chunk_size - 1],
                   static_cast<int64_t>((viewer::__chunk_size - 1) * 2));
    TEST_ASSERT_EQ(col[viewer::__chunk_size],
                   static_cast<int64_t>(viewer::__chunk_size * 2));
    TEST_ASSERT_EQ(col[n - 1],
                   static_cast<int64_t>((n - 1) * 2));
}

// ---------- back() ----------
TEST(ColumnPush, BackReturnsLast)
{
    viewer::Column<int64_t> col;
    col.push_back(10);
    col.push_back(20);
    TEST_ASSERT_EQ(col.back(), 20);
    col.push_back(30);
    TEST_ASSERT_EQ(col.back(), 30);
}

TEST(ColumnPush, BackModify)
{
    viewer::Column<int64_t> col;
    col.push_back(10);
    col.back() = 50;
    TEST_ASSERT_EQ(col[0], 50);
}

TEST(ColumnPush, BackEmpty)
{
    viewer::Column<int64_t> col;
    TEST_ASSERT_EQ(std::as_const(col).back(), 0);  // 返回静态空值
}

} // TEST_GROUP(ColumnPush)