#include "test_case.h"
#include "code_viewer/datamgr/data_struct.hpp"

TEST_GROUP(DataChunk)
{

TEST(DataChunk, DefaultConstructor)
{
    viewer::DataChunk<int64_t> chunk;
    TEST_ASSERT_EQ(chunk._size, 0u);
    TEST_ASSERT_FALSE(chunk.full());
}

TEST(DataChunk, Capacity)
{
    viewer::DataChunk<int64_t> chunk;
    TEST_ASSERT_EQ(chunk.capacity(), viewer::__chunk_size);
    TEST_ASSERT_EQ(viewer::DataChunk<int64_t>::CHUNK_CAPACITY, viewer::__chunk_size);
}

TEST(DataChunk, Full)
{
    viewer::DataChunk<int64_t> chunk;
    chunk._size = viewer::__chunk_size;
    TEST_ASSERT_TRUE(chunk.full());
}

TEST(DataChunk, NotFull)
{
    viewer::DataChunk<int64_t> chunk;
    chunk._size = viewer::__chunk_size - 1;
    TEST_ASSERT_FALSE(chunk.full());
}

TEST(DataChunk, DoubleType)
{
    viewer::DataChunk<double> chunk;
    TEST_ASSERT_EQ(chunk._size, 0u);
    TEST_ASSERT_FALSE(chunk.full());
    TEST_ASSERT_EQ(chunk.capacity(), viewer::__chunk_size);
}

} // TEST_GROUP(DataChunk)