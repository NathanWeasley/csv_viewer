#pragma once

#include "code_viewer/datamgr/data_struct.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

namespace viewer
{

// ============================================================
// 跨行函数指针类型
//   cols     : 参数列指针数组（栈上分配，零堆）
//   colCount : 参数个数
//   rowCount : 行数
// ============================================================
using CrossRowComputeFn = std::unique_ptr<Column<double>>(*)(
    AbstractColumn* const* cols, uint8_t colCount, size_t rowCount);

// ============================================================
// CustomFuncEntry: 编译期固定大小的函数注册项
// ============================================================
struct CustomFuncEntry
{
    std::string_view    name;
    uint8_t             argCount;
    CrossRowComputeFn   compute;
};

// ============================================================
// CustomFuncRegistry: 零堆分配注册表
// 所有条目在 .data/.bss 段，cache 友好
// ============================================================
struct CustomFuncRegistry
{
    static constexpr size_t MAX_FUNCS = 16;
    static inline std::array<CustomFuncEntry, MAX_FUNCS> s_entries = {};
    static inline uint8_t s_count = 0;

    static const std::array<CustomFuncEntry, MAX_FUNCS>& entries() noexcept { return s_entries; }
    static uint8_t count() noexcept { return s_count; }

    static void add(CustomFuncEntry e) noexcept
    {
        if (s_count < MAX_FUNCS)
            s_entries[s_count++] = e;
    }
};

} // namespace viewer

// ============================================================
// REGISTER_CUSTOM_EXPR_FUNC: 自动注册宏
// 利用 static 初始化在 main() 前完成注册
// 用法: REGISTER_CUSTOM_EXPR_FUNC(FdiffFunc);
// ============================================================
#define REGISTER_CUSTOM_EXPR_FUNC(FuncType)                        \
    namespace {                                                     \
    const int __reg_##FuncType = [] {                               \
        ::viewer::CustomFuncRegistry::add({                         \
            FuncType::name, FuncType::argCount,                     \
            &FuncType::compute                                      \
        });                                                         \
        return 0;                                                   \
    }();                                                            \
    }