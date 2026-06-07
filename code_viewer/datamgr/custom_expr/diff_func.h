#pragma once

#include "code_viewer/datamgr/custom_expr/custom_func.h"
#include <cstdint>
#include <memory>

namespace viewer
{

// ============================================================
// FdiffFunc: 前向差分  fdiff(col) = col[i+1] - col[i]
// 最后一行填充 0.0
// ============================================================
struct FdiffFunc
{
    static constexpr std::string_view name = "fdiff";
    static constexpr uint8_t argCount = 1;

    inline static std::unique_ptr<Column<double>> compute(
        AbstractColumn* const* cols, uint8_t /*colCount*/, size_t rowCount)
    {
        auto* col = cols[0];
        auto result = std::make_unique<Column<double>>(ColumnType::Float64);

        if (rowCount == 0)
            return result;

        for (size_t i = 0; i + 1 < rowCount; ++i)
            result->push_back(col->getDouble(i + 1) - col->getDouble(i));

        // 最后一行填充 0.0
        result->push_back(0.0);

        return result;
    }
};

// ============================================================
// BdiffFunc: 后向差分  bdiff(col) = col[i] - col[i-1]
// 第一行填充 0.0
// ============================================================
struct BdiffFunc
{
    static constexpr std::string_view name = "bdiff";
    static constexpr uint8_t argCount = 1;

    inline static std::unique_ptr<Column<double>> compute(
        AbstractColumn* const* cols, uint8_t /*colCount*/, size_t rowCount)
    {
        auto* col = cols[0];
        auto result = std::make_unique<Column<double>>(ColumnType::Float64);

        if (rowCount == 0)
            return result;

        // 第一行填充 0.0
        result->push_back(0.0);

        for (size_t i = 1; i < rowCount; ++i)
            result->push_back(col->getDouble(i) - col->getDouble(i - 1));

        return result;
    }
};

} // namespace viewer