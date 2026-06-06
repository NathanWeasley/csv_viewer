#include "test_case.h"
#include "code_viewer/datamgr/data_struct.hpp"
#include <limits>
#include <cmath>

TEST_GROUP(ColumnLifecycle)
{

// ---------- clear() ----------
TEST(ColumnLifecycle, ClearEmpty)
{
    viewer::Column<int64_t> col;
    col.clear();
    TEST_ASSERT_EQ(col.size(), 0u);
    TEST_ASSERT_TRUE(col.empty());
}

TEST(ColumnLifecycle, ClearWithData)
{
    viewer::Column<int64_t> col;
    col.push_back(1);
    col.push_back(2);
    col.push_back(3);
    col.clear();
    TEST_ASSERT_EQ(col.size(), 0u);
    TEST_ASSERT_TRUE(col.empty());
}

TEST(ColumnLifecycle, PushAfterClear)
{
    viewer::Column<int64_t> col;
    col.push_back(1);
    col.clear();
    col.push_back(42);
    TEST_ASSERT_EQ(col.size(), 1u);
    TEST_ASSERT_EQ(col[0], 42);
}

// ---------- pushFromString int64_t ----------
TEST(ColumnLifecycle, PushFromStringIntValid)
{
    viewer::Column<int64_t> col;
    col.pushFromString("12345");
    TEST_ASSERT_EQ(col.size(), 1u);
    TEST_ASSERT_EQ(col[0], 12345);
}

TEST(ColumnLifecycle, PushFromStringIntNegative)
{
    viewer::Column<int64_t> col;
    col.pushFromString("-9999");
    TEST_ASSERT_EQ(col[0], -9999);
}

TEST(ColumnLifecycle, PushFromStringIntGarbage)
{
    viewer::Column<int64_t> col;
    col.pushFromString("not_a_number");
    TEST_ASSERT_EQ(col[0], 0);  // 不匹配 → 推 0
}

// ---------- pushFromString double ----------
TEST(ColumnLifecycle, PushFromStringDoubleValid)
{
    viewer::Column<double> col;
    col.pushFromString("3.14159");
    TEST_ASSERT_NEAR(col[0], 3.14159, 1e-12);
}

TEST(ColumnLifecycle, PushFromStringDoubleScientific)
{
    viewer::Column<double> col;
    col.pushFromString("1e10");
    TEST_ASSERT_NEAR(col[0], 1e10, 1e-2);
}

TEST(ColumnLifecycle, PushFromStringDoubleNaN)
{
    viewer::Column<double> col;
    col.pushFromString("hello");
    TEST_ASSERT_TRUE(std::isnan(col[0]));
}

// ---------- getDouble / getInt64 ----------
TEST(ColumnLifecycle, GetDoubleFromInt)
{
    viewer::Column<int64_t> col;
    col.push_back(100);
    TEST_ASSERT_NEAR(col.getDouble(0), 100.0, 1e-12);
}

TEST(ColumnLifecycle, GetDoubleFromFloat)
{
    viewer::Column<double> col;
    col.push_back(2.71828);
    TEST_ASSERT_NEAR(col.getDouble(0), 2.71828, 1e-12);
}

TEST(ColumnLifecycle, GetInt64FromInt)
{
    viewer::Column<int64_t> col;
    col.push_back(-50);
    TEST_ASSERT_EQ(col.getInt64(0), -50);
}

TEST(ColumnLifecycle, GetInt64FromFloat)
{
    viewer::Column<double> col;
    col.push_back(3.9);
    TEST_ASSERT_EQ(col.getInt64(0), 3);
}

// ---------- append ----------
TEST(ColumnLifecycle, AppendVector)
{
    viewer::Column<int64_t> col;
    std::vector<int64_t> vec = {1, 2, 3, 4, 5};
    col.append(vec);
    TEST_ASSERT_EQ(col.size(), 5u);
    for (size_t i = 0; i < 5; ++i)
        TEST_ASSERT_EQ(col[i], static_cast<int64_t>(i + 1));
}

TEST(ColumnLifecycle, AppendIterators)
{
    viewer::Column<double> col;
    double arr[] = {1.0, 2.0, 3.0};
    col.append(std::begin(arr), std::end(arr));
    TEST_ASSERT_EQ(col.size(), 3u);
    TEST_ASSERT_NEAR(col[0], 1.0, 1e-12);
    TEST_ASSERT_NEAR(col[1], 2.0, 1e-12);
    TEST_ASSERT_NEAR(col[2], 3.0, 1e-12);
}

TEST(ColumnLifecycle, AppendEmpty)
{
    viewer::Column<int64_t> col;
    std::vector<int64_t> emptyVec;
    col.append(emptyVec);
    TEST_ASSERT_EQ(col.size(), 0u);
    TEST_ASSERT_TRUE(col.empty());
}

} // TEST_GROUP(ColumnLifecycle)