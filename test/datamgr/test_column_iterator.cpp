#include "test_case.h"
#include "code_viewer/datamgr/data_struct.hpp"
#include <algorithm>

TEST_GROUP(ColumnIterator)
{

// ---------- const_iterator ----------
TEST(ColumnIterator, ConstBeginEnd)
{
    viewer::Column<int64_t> col;
    for (int64_t i = 0; i < 10; ++i)
        col.push_back(i);

    auto it = col.begin();
    auto end = col.end();
    TEST_ASSERT_NE(it, end);
    int64_t expected = 0;
    while (it != end)
    {
        TEST_ASSERT_EQ(*it, expected++);
        ++it;
    }
    TEST_ASSERT_EQ(expected, 10);
}

TEST(ColumnIterator, ConstRangeFor)
{
    viewer::Column<double> col;
    col.push_back(1.1);
    col.push_back(2.2);
    col.push_back(3.3);

    double sum = 0.0;
    for (auto v : col)
        sum += v;
    TEST_ASSERT_NEAR(sum, 6.6, 1e-12);
}

TEST(ColumnIterator, ConstPostIncrement)
{
    viewer::Column<int64_t> col;
    col.push_back(100);
    col.push_back(200);

    auto it = col.begin();
    auto prev = it++;
    TEST_ASSERT_EQ(*prev, 100);
    TEST_ASSERT_EQ(*it, 200);
}

TEST(ColumnIterator, ConstDecrement)
{
    viewer::Column<int64_t> col;
    col.push_back(10);
    col.push_back(20);
    col.push_back(30);

    auto it = col.end();
    --it;
    TEST_ASSERT_EQ(*it, 30);
    --it;
    TEST_ASSERT_EQ(*it, 20);
    auto prev = it--;
    TEST_ASSERT_EQ(*prev, 20);
    TEST_ASSERT_EQ(*it, 10);
}

TEST(ColumnIterator, ConstRandomAccess)
{
    viewer::Column<int64_t> col;
    for (int64_t i = 0; i < 100; ++i)
        col.push_back(i);

    auto it = col.begin();
    it += 50;
    TEST_ASSERT_EQ(*it, 50);
    TEST_ASSERT_EQ(it[5], 55);
    it -= 25;
    TEST_ASSERT_EQ(*it, 25);

    // difference
    auto diff = col.end() - col.begin();
    TEST_ASSERT_EQ(static_cast<size_t>(diff), 100u);
}

TEST(ColumnIterator, ConstComparison)
{
    viewer::Column<int64_t> col;
    col.push_back(1);
    col.push_back(2);
    col.push_back(3);

    auto a = col.begin();
    auto b = col.begin();
    auto c = col.end();
    auto d = col.begin(); ++d;
    TEST_ASSERT_TRUE(a == b);
    TEST_ASSERT_FALSE(a == c);
    TEST_ASSERT_TRUE(a != c);
    TEST_ASSERT_TRUE(a < d);
    TEST_ASSERT_TRUE(d > a);
    TEST_ASSERT_TRUE(a <= a);
    TEST_ASSERT_TRUE(a >= a);
    TEST_ASSERT_TRUE(a <= d);
}

TEST(ColumnIterator, ConstGlobalIndex)
{
    viewer::Column<int64_t> col;
    for (int64_t i = 0; i < 5; ++i)
        col.push_back(i * 10);

    auto it = col.begin();
    TEST_ASSERT_EQ(it.globalIndex(), 0u);
    it += 3;
    TEST_ASSERT_EQ(it.globalIndex(), 3u);
}

// ---------- iterator (mutable) ----------
TEST(ColumnIterator, MutableBeginEnd)
{
    viewer::Column<int64_t> col;
    col.push_back(1);
    col.push_back(2);
    col.push_back(3);

    auto it = col.begin();
    TEST_ASSERT_EQ(*it, 1);
    *it = 99;
    ++it;
    *it = 88;
    ++it;
    *it = 77;

    TEST_ASSERT_EQ(col[0], 99);
    TEST_ASSERT_EQ(col[1], 88);
    TEST_ASSERT_EQ(col[2], 77);
}

TEST(ColumnIterator, MutableRandomAccess)
{
    viewer::Column<double> col;
    col.push_back(1.0);
    col.push_back(2.0);
    col.push_back(3.0);
    col.push_back(4.0);

    auto it = col.begin();
    it[1] = 99.9;
    TEST_ASSERT_NEAR(col[1], 99.9, 1e-12);
}

// ---------- iterator → const_iterator implicit conversion ----------
TEST(ColumnIterator, ImplicitToConst)
{
    viewer::Column<int64_t> col;
    col.push_back(5);
    col.push_back(10);

    viewer::Column<int64_t>::iterator it = col.begin();
    viewer::Column<int64_t>::const_iterator cit = it;  // 隐式转换
    TEST_ASSERT_EQ(*cit, 5);
    ++cit;
    TEST_ASSERT_EQ(*cit, 10);
}

// ---------- std::algorithm compatibility ----------
TEST(ColumnIterator, StdFind)
{
    viewer::Column<int64_t> col;
    col.push_back(3);
    col.push_back(7);
    col.push_back(12);
    col.push_back(7);
    col.push_back(19);

    auto it = std::find(col.begin(), col.end(), 12);
    TEST_ASSERT_NE(it, col.end());
    TEST_ASSERT_EQ(*it, 12);
    TEST_ASSERT_EQ(it.globalIndex(), 2u);
}

TEST(ColumnIterator, StdAccumulate)
{
    viewer::Column<double> col;
    col.push_back(1.0);
    col.push_back(2.0);
    col.push_back(3.0);
    col.push_back(4.0);

    double sum = 0.0;
    for (auto it = col.begin(); it != col.end(); ++it)
        sum += *it;
    TEST_ASSERT_NEAR(sum, 10.0, 1e-12);
}

// ---------- cross-chunk iteration ----------
TEST(ColumnIterator, CrossChunkIteration)
{
    viewer::Column<int64_t> col;
    size_t n = viewer::__chunk_size + 100;
    for (size_t i = 0; i < n; ++i)
        col.push_back(static_cast<int64_t>(i));

    size_t count = 0;
    for (auto v : col)
    {
        TEST_ASSERT_EQ(v, static_cast<int64_t>(count));
        ++count;
    }
    TEST_ASSERT_EQ(count, n);
}

} // TEST_GROUP(ColumnIterator)