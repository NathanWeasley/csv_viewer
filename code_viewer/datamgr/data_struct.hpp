#pragma once

#include "code_viewer/base/base_def.h"
#include <vector>

namespace viewer
{

template <typename T>
struct DataChunk
{
    std::array<T, __chunk_size> _data;
};

template <typename T>
struct ColumnData
{
    std::vector<DataChunk<T>> _column;
};


}

