#include "code_viewer/plotmgr/expr/expr_manager.h"
#include "code_viewer/datamgr/data_manager.h"
#include "code_viewer/datamgr/custom_expr/custom_func.h"
#include "code_viewer/datamgr/custom_expr/diff_func.h"
#include "code_viewer/base/exprtk_keywords.h"
#include "code_viewer/base/trace_logger.h"
#include "extra/exprtk/exprtk.hpp"
#include <cctype>
#include <unordered_set>
#include <algorithm>

namespace viewer
{

// ============================================================
// 表达式访问
// ============================================================

PlotExpression& ExprManager::getOrCreate(const std::string& itemName, DataManager& dm)
{
    Q_UNUSED(dm);
    auto it = m_exprs.find(itemName);
    if (it == m_exprs.end())
    {
        // 首次创建时没有计算缓冲区，graph 直接从 DataManager 读取原始列。
        PlotExpression pe;
        pe.expressionText = itemName;
        pe.isEdited = false;

        auto [insertedIt, _] = m_exprs.insert({itemName, std::move(pe)});
        trace::write(trace::Category::Expression,
                     QString("manager create item=\"%1\" total=%2")
                         .arg(QString::fromStdString(itemName)).arg(m_exprs.size()));
        return insertedIt->second;
    }

    return it->second;
}

PlotExpression* ExprManager::get(const std::string& itemName)
{
    auto it = m_exprs.find(itemName);
    if (it != m_exprs.end())
        return &it->second;
    return nullptr;
}

bool ExprManager::has(const std::string& itemName) const
{
    return m_exprs.count(itemName) > 0;
}

// ============================================================
// 表达式编辑
// ============================================================

void ExprManager::setExpressionText(const std::string& itemName, const std::string& text)
{
    auto it = m_exprs.find(itemName);
    if (it == m_exprs.end())
    {
        trace::write(trace::Category::Expression,
                     QString("manager set text ignored: item missing item=\"%1\"")
                         .arg(QString::fromStdString(itemName)));
        return;
    }

    it->second.expressionText = text;
    it->second.isEdited = (text != itemName);

    if (onExpressionTextChanged)
        onExpressionTextChanged(itemName, text);
}

bool ExprManager::validate(const std::string& exprStr, DataManager& dm)
{
    trace::write(trace::Category::Expression,
                 QString("manager validate enter length=%1 rows=%2")
                     .arg(exprStr.size()).arg(dm.GetRowCount()));
    // 空字符串视为无效
    if (exprStr.empty())
        return false;

    size_t rowCount = dm.GetRowCount();
    if (rowCount == 0)
        return false;

    // 自定义函数预处理
    std::string processedExpr = preprocessCustomFuncs(exprStr, dm, rowCount);
    trace::write(trace::Category::Expression,
                 QString("manager validate preprocessed inputLength=%1 processedLength=%2 tempColumns=%3")
                     .arg(exprStr.size()).arg(processedExpr.size()).arg(m_tempCols.size()));

    // 提取引用列名（从预处理后的表达式中扫描）
    std::vector<std::string> refCols;
    {
        const auto& colNames = dm.GetColumnNames();
        std::unordered_set<std::string> validCols(colNames.begin(), colNames.end());
        // 添加临时列名
        for (const auto& [name, _] : m_tempCols)
            validCols.insert(name);

        auto funcNames = customFuncNames();

        // exprtk 内置关键字
        const auto& kKeywords = GetExprtkKeywords();

        std::unordered_set<std::string> seen;
        const char* p = processedExpr.c_str();
        const char* end = p + processedExpr.size();

        while (p < end)
        {
            while (p < end && !std::isalpha(static_cast<unsigned char>(*p)) && *p != '_')
                ++p;
            if (p >= end)
                break;
            const char* start = p;
            while (p < end && (std::isalnum(static_cast<unsigned char>(*p)) || *p == '_'))
                ++p;
            std::string token(start, p - start);
            if (token.empty())
                continue;
            if (kKeywords.find(token) != kKeywords.end())
                continue;
            if (validCols.find(token) != validCols.end() && seen.find(token) == seen.end())
            {
                refCols.push_back(token);
                seen.insert(token);
            }
        }
    }

    // 验证所有引用列存在（DataManager 或临时列）
    for (const auto& colName : refCols)
    {
        if (dm.GetColumnIndex(colName) == static_cast<size_t>(-1)
            && m_tempCols.find(colName) == m_tempCols.end())
            return false;
    }

    // exprtk 编译检查
    exprtk::symbol_table<double> symbolTable;
    std::vector<double> varValues(refCols.size(), 0.0);

    for (size_t i = 0; i < refCols.size(); ++i)
    {
        symbolTable.add_variable(refCols[i], varValues[i]);
    }
    symbolTable.add_constants();

    exprtk::expression<double> expression;
    expression.register_symbol_table(symbolTable);

    exprtk::parser<double> parser;
    if (!parser.compile(processedExpr, expression))
    {
        trace::write(trace::Category::Expression,
                     QString("manager validate leave result=compile-failed refs=%1")
                         .arg(refCols.size()));
        return false;
    }

    trace::write(trace::Category::Expression,
                 QString("manager validate leave result=valid refs=%1").arg(refCols.size()));
    return true;
}

bool ExprManager::recompute(const std::string& itemName, DataManager& dm)
{
    trace::write(trace::Category::Expression,
                 QString("manager recompute enter item=\"%1\" rows=%2")
                     .arg(QString::fromStdString(itemName)).arg(dm.GetRowCount()));
    auto it = m_exprs.find(itemName);
    if (it == m_exprs.end())
    {
        trace::write(trace::Category::Expression, "manager recompute leave result=item-missing");
        return false;
    }

    PlotExpression& pe = it->second;
    const std::string& exprStr = pe.expressionText;

    size_t rowCount = dm.GetRowCount();
    if (rowCount == 0)
        return false;

    // 未编辑时 graph 直接从 DataManager 读取；保留已有缓冲区供后续编辑复用。
    if (!pe.isEdited)
    {
        if (onExpressionRecomputed)
            onExpressionRecomputed(itemName);
        return true;
    }

    // ---- 已编辑的表达式：预处理自定义函数 + exprtk 计算 ----

    // 自定义函数预处理
    std::string processedExpr = preprocessCustomFuncs(exprStr, dm, rowCount);
    trace::write(trace::Category::Expression,
                 QString("manager recompute preprocessed item=\"%1\" processedLength=%2 tempColumns=%3")
                     .arg(QString::fromStdString(itemName)).arg(processedExpr.size()).arg(m_tempCols.size()));

    // 提取引用列名（从预处理后的表达式）
    std::vector<std::string> refCols;
    {
        std::unordered_set<std::string> validCols;
        for (const auto& name : dm.GetColumnNames())
            validCols.insert(name);
        for (const auto& [name, _] : m_tempCols)
            validCols.insert(name);

        const auto& kKeywords = GetExprtkKeywords();

        std::unordered_set<std::string> seen;
        const char* p = processedExpr.c_str();
        const char* end = p + processedExpr.size();

        while (p < end)
        {
            while (p < end && !std::isalpha(static_cast<unsigned char>(*p)) && *p != '_')
                ++p;
            if (p >= end) break;
            const char* start = p;
            while (p < end && (std::isalnum(static_cast<unsigned char>(*p)) || *p == '_'))
                ++p;
            std::string token(start, p - start);
            if (token.empty()) continue;
            if (kKeywords.find(token) != kKeywords.end()) continue;
            if (validCols.find(token) != validCols.end() && seen.find(token) == seen.end())
            {
                refCols.push_back(token);
                seen.insert(token);
            }
        }
    }

    // 构建 symbol table：DataManager 列 + 临时列
    exprtk::symbol_table<double> symbolTable;
    std::vector<double> varValues(refCols.size(), 0.0);

    for (size_t i = 0; i < refCols.size(); ++i)
    {
        symbolTable.add_variable(refCols[i], varValues[i]);
    }
    symbolTable.add_constants();

    exprtk::expression<double> expression;
    expression.register_symbol_table(symbolTable);

    exprtk::parser<double> parser;
    if (!parser.compile(processedExpr, expression))
    {
        trace::write(trace::Category::Expression,
                     QString("manager recompute leave item=\"%1\" result=compile-failed refs=%2")
                         .arg(QString::fromStdString(itemName)).arg(refCols.size()));
        return false;
    }

    // 结果列只在首次计算或数据长度变化时分配；频繁重算复用原缓冲区。
    if (!pe.computedData)
        pe.computedData = std::make_unique<Column>(rowCount);
    pe.computedData->beginOverwrite(rowCount);

    trace::write(trace::Category::Expression,
                 QString("manager recompute evaluate item=\"%1\" rows=%2 refs=%3 output=0x%4")
                     .arg(QString::fromStdString(itemName)).arg(rowCount).arg(refCols.size())
                     .arg(reinterpret_cast<quintptr>(pe.computedData.get()), 0, 16));

    for (size_t row = 0; row < rowCount; ++row)
    {
        for (size_t i = 0; i < refCols.size(); ++i)
        {
            // 先在 DataManager 中查找，再在临时列中查找
            size_t colIdx = dm.GetColumnIndex(refCols[i]);
            if (colIdx != static_cast<size_t>(-1))
            {
                varValues[i] = dm.GetValueAsDouble(colIdx, row);
            }
            else
            {
                auto tcIt = m_tempCols.find(refCols[i]);
                if (tcIt != m_tempCols.end() && tcIt->second)
                    varValues[i] = tcIt->second->getDouble(row);
            }
        }
        (*pe.computedData)[row] = expression.value();
    }
    pe.computedData->recalcMinMax();

    if (onExpressionRecomputed)
        onExpressionRecomputed(itemName);

    trace::write(trace::Category::Expression,
                 QString("manager recompute leave item=\"%1\" result=success outputRows=%2")
                     .arg(QString::fromStdString(itemName)).arg(pe.computedData->size()));
    return true;
}

// ============================================================
// 数据拷贝
// ============================================================

PlotExpression ExprManager::copy(const std::string& itemName) const
{
    PlotExpression result;

    auto it = m_exprs.find(itemName);
    if (it == m_exprs.end())
        return result;

    const PlotExpression& src = it->second;
    result.expressionText = src.expressionText;
    result.isEdited = src.isEdited;

    // 仅复制当前正在使用的计算结果；未编辑状态不复制闲置缓冲区。
    if (src.isEdited && src.computedData && src.computedData->size() > 0)
    {
        result.computedData = std::make_unique<Column>(*src.computedData);
    }
    // else: 保持 nullptr

    return result;
}

std::unordered_map<std::string, PlotExpression> ExprManager::copyAll() const
{
    std::unordered_map<std::string, PlotExpression> result;

    for (const auto& [name, pe] : m_exprs)
    {
        PlotExpression copyPE;
        copyPE.expressionText = pe.expressionText;
        copyPE.isEdited = pe.isEdited;

        if (pe.isEdited && pe.computedData && pe.computedData->size() > 0)
        {
            copyPE.computedData = std::make_unique<Column>(*pe.computedData);
        }
        // else: 保持 nullptr（未编辑时 graph 应从 DataManager 读取）

        result.insert({name, std::move(copyPE)});
    }

    return result;
}

void ExprManager::insertAll(std::unordered_map<std::string, PlotExpression>&& exprs)
{
    trace::write(trace::Category::Expression,
                 QString("manager insert all expressions count=%1").arg(exprs.size()));
    m_exprs = std::move(exprs);
}

// ============================================================
// 生命周期
// ============================================================

void ExprManager::removeItem(const std::string& itemName)
{
    trace::write(trace::Category::Expression,
                 QString("manager remove item=\"%1\" existed=%2 totalBefore=%3")
                     .arg(QString::fromStdString(itemName)).arg(m_exprs.count(itemName) != 0)
                     .arg(m_exprs.size()));
    m_exprs.erase(itemName);
}

void ExprManager::clearAll()
{
    trace::write(trace::Category::Expression,
                 QString("manager clear all count=%1").arg(m_exprs.size()));
    m_exprs.clear();
}

// ============================================================
// 私有
// ============================================================

std::vector<std::string> ExprManager::extractRefColumns(const std::string& exprStr, DataManager& dm) const
{
    const auto& kKeywords = GetExprtkKeywords();

    std::vector<std::string> result;
    std::unordered_set<std::string> seen;

    // 从 DataManager 获取有效列名集合
    const auto& colNames = dm.GetColumnNames();
    std::unordered_set<std::string> validCols(colNames.begin(), colNames.end());

    const char* p = exprStr.c_str();
    const char* end = p + exprStr.size();

    while (p < end)
    {
        while (p < end && !std::isalpha(static_cast<unsigned char>(*p)) && *p != '_')
            ++p;
        if (p >= end)
            break;
        const char* start = p;
        while (p < end && (std::isalnum(static_cast<unsigned char>(*p)) || *p == '_'))
            ++p;
        std::string token(start, p - start);
        if (token.empty())
            continue;
        if (kKeywords.find(token) != kKeywords.end())
            continue;
        if (validCols.find(token) != validCols.end() && seen.find(token) == seen.end())
        {
            result.push_back(token);
            seen.insert(token);
        }
    }

    return result;
}

std::unordered_set<std::string> ExprManager::customFuncNames() const
{
    std::unordered_set<std::string> names;
    for (uint8_t i = 0; i < CustomFuncRegistry::count(); ++i)
    {
        names.insert(std::string(CustomFuncRegistry::entries()[i].name));
    }
    return names;
}

std::string ExprManager::preprocessCustomFuncs(const std::string& exprStr, DataManager& dm, size_t rowCount)
{
    // 确保自定义函数已注册
    EnsureDiffFuncsLinked();

    std::string result = exprStr;
    m_tempCols.clear();

    for (uint8_t fi = 0; fi < CustomFuncRegistry::count(); ++fi)
    {
        const auto& entry = CustomFuncRegistry::entries()[fi];
        std::string_view funcName = entry.name;
        std::string searchPrefix = std::string(funcName) + "(";

        size_t searchPos = 0;
        while (true)
        {
            size_t callStart = result.find(searchPrefix, searchPos);
            if (callStart == std::string::npos)
                break;

            // 找到参数列表的起始和结束位置
            size_t argsStart = callStart + searchPrefix.size();
            size_t argsEnd = argsStart;
            int parenDepth = 1;

            while (argsEnd < result.size() && parenDepth > 0)
            {
                if (result[argsEnd] == '(') ++parenDepth;
                else if (result[argsEnd] == ')') --parenDepth;
                if (parenDepth > 0) ++argsEnd;
            }

            if (parenDepth != 0)
            {
                searchPos = callStart + searchPrefix.size();
                continue;
            }

            std::string argsStr = result.substr(argsStart, argsEnd - argsStart);

            // 分割参数（按逗号分隔）
            std::vector<std::string> argNames;
            size_t commaPos = 0;
            while (true)
            {
                while (commaPos < argsStr.size() && argsStr[commaPos] == ' ')
                    ++commaPos;

                size_t nextComma = argsStr.find(',', commaPos);
                std::string arg;
                if (nextComma == std::string::npos)
                {
                    arg = argsStr.substr(commaPos);
                }
                else
                {
                    arg = argsStr.substr(commaPos, nextComma - commaPos);
                }

                // 去除首尾空格
                while (!arg.empty() && arg.front() == ' ') arg.erase(0, 1);
                while (!arg.empty() && arg.back() == ' ') arg.pop_back();

                if (!arg.empty())
                    argNames.push_back(arg);

                if (nextComma == std::string::npos)
                    break;
                commaPos = nextComma + 1;
            }

            // 验证参数个数
            if (argNames.size() != entry.argCount)
            {
                searchPos = argsEnd + 1;
                continue;
            }

            // 验证所有参数列存在
            bool allColsExist = true;
            std::vector<Column*> cols;
            cols.reserve(argNames.size());

            for (size_t ai = 0; ai < argNames.size(); ++ai)
            {
                auto colIt = dm.GetColumnIndex(argNames[ai]);
                if (colIt == static_cast<size_t>(-1))
                {
                    allColsExist = false;
                    break;
                }
                cols.push_back(dm.GetColumn(colIt));
            }

            if (!allColsExist)
            {
                searchPos = argsEnd + 1;
                continue;
            }

            // 调用 compute 生成临时列
            auto tempCol = entry.compute(cols.data(), static_cast<uint8_t>(cols.size()), rowCount);
            if (!tempCol)
            {
                searchPos = argsEnd + 1;
                continue;
            }

            // 生成临时列名
            std::string tempName = "tmp_" + std::string(funcName) + "_";
            for (size_t ai = 0; ai < argNames.size(); ++ai)
            {
                if (ai > 0) tempName += "_";
                tempName += argNames[ai];
            }

            // 确保唯一性
            int suffix = 0;
            std::string uniqueName = tempName;
            while (dm.GetColumnIndex(uniqueName) != static_cast<size_t>(-1)
                   || m_tempCols.find(uniqueName) != m_tempCols.end())
            {
                ++suffix;
                uniqueName = tempName + std::to_string(suffix);
            }

            // 存储临时列
            m_tempCols[uniqueName] = std::move(tempCol);

            // 替换表达式中的函数调用为列名
            result.replace(callStart, (argsEnd + 1) - callStart, uniqueName);

            // 从替换位置之后继续搜索
            searchPos = callStart + uniqueName.size();
        }
    }

    return result;
}

} // namespace viewer
