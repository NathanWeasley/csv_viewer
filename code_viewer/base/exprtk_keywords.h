#pragma once

#include <unordered_set>
#include <string>

namespace viewer
{

// ============================================================
// exprtk 内置关键字的集中定义
// 在 data_manager.cpp 和 expr_manager.cpp 中共享引用
// ============================================================
inline const std::unordered_set<std::string>& GetExprtkKeywords()
{
    static const std::unordered_set<std::string> kKeywords =
    {
        // 数学函数
        "sin", "cos", "tan", "abs", "acos", "asin", "atan", "atan2",
        "ceil", "floor", "round", "trunc", "frac", "sgn",
        "cosh", "sinh", "tanh", "acosh", "asinh", "atanh",
        "exp", "expm1", "log", "log10", "log1p", "log2",
        "sqrt", "cbrt", "pow", "hypot",
        "min", "max", "clamp", "inrange",
        "deg2rad", "rad2deg",
        "cot", "csc", "sec", "acot", "acsc", "asec",
        "coth", "csch", "sech", "acoth", "acsch", "asech",
        "mod", "erf", "erfc", "ncdf",

        // 控制流
        "if", "switch", "case", "default",
        "while", "repeat", "until",
        "var", "return",

        // 逻辑
        "and", "nand", "or", "nor", "xor", "xnor", "not",
        "mand", "mor",

        // 常量
        "pi", "epsilon", "inf", "nan",
        "true", "false",

        // 保留
        "break", "continue", "for",
        "e",  // exprtk's 'e' constant
    };

    return kKeywords;
}

} // namespace viewer