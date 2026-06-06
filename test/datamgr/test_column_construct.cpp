#include "test_case.h"
#include "code_viewer/datamgr/data_struct.hpp"
#include <memory>

TEST_GROUP(ColumnConstruct)
{

TEST(ColumnConstruct, Int64Default)
{
    viewer::Column<int64_t> col;
    TEST_ASSERT_EQ(col.type(), viewer::ColumnType::Int64);
    TEST_ASSERT_EQ(col.size(), 0u);
    TEST_ASSERT_TRUE(col.empty());
}

TEST(ColumnConstruct, Float64Default)
{
    viewer::Column<double> col;
    TEST_ASSERT_EQ(col.type(), viewer::ColumnType::Float64);
    TEST_ASSERT_EQ(col.size(), 0u);
    TEST_ASSERT_TRUE(col.empty());
}

TEST(ColumnConstruct, Int64WithType)
{
    viewer::Column<int64_t> col(viewer::ColumnType::Int64);
    TEST_ASSERT_EQ(col.type(), viewer::ColumnType::Int64);
}

TEST(ColumnConstruct, Float64WithType)
{
    viewer::Column<double> col(viewer::ColumnType::Float64);
    TEST_ASSERT_EQ(col.type(), viewer::ColumnType::Float64);
}

TEST(ColumnConstruct, TypeName)
{
    viewer::Column<int64_t> ci;
    viewer::Column<double>  cf;
    TEST_ASSERT_EQ(ci.typeName(), std::string("int64"));
    TEST_ASSERT_EQ(cf.typeName(), std::string("float64"));
}

TEST(ColumnConstruct, AbstractType)
{
    viewer::Column<int64_t> col;
    const viewer::AbstractColumn& a = col;
    TEST_ASSERT_EQ(a.type(), viewer::ColumnType::Int64);
    TEST_ASSERT_EQ(a.size(), 0u);
    TEST_ASSERT_TRUE(a.empty());
}

TEST(ColumnConstruct, CloneEmptyInt)
{
    viewer::Column<int64_t> col;
    auto cloned = col.cloneEmpty();
    TEST_ASSERT_TRUE(cloned != nullptr);
    TEST_ASSERT_EQ(cloned->type(), viewer::ColumnType::Int64);
    TEST_ASSERT_TRUE(cloned->empty());
}

TEST(ColumnConstruct, CloneEmptyFloat)
{
    viewer::Column<double> col;
    auto cloned = col.cloneEmpty();
    TEST_ASSERT_TRUE(cloned != nullptr);
    TEST_ASSERT_EQ(cloned->type(), viewer::ColumnType::Float64);
    TEST_ASSERT_TRUE(cloned->empty());
}

} // TEST_GROUP(ColumnConstruct)