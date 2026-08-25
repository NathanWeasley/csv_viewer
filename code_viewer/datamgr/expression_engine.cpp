#include "code_viewer/datamgr/expression_engine.h"

#include "code_viewer/base/exprtk_keywords.h"
#include "extra/exprtk/exprtk.hpp"

#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace viewer
{
namespace
{

using ColumnMap = std::unordered_map<std::string, ExpressionColumnView>;
using TemporaryColumns = std::unordered_map<std::string, std::vector<double>>;

std::string trim(std::string text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.pop_back();
    return text;
}

bool isIdentifier(const std::string& text)
{
    if (text.empty()
        || (!std::isalpha(static_cast<unsigned char>(text.front()))
            && text.front() != '_'))
    {
        return false;
    }
    return std::all_of(text.begin() + 1, text.end(), [](char c)
    {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    });
}

ExpressionEngineResult preprocessDifferenceFunction(
    std::string& expression,
    const char* functionName,
    bool forward,
    const ColumnMap& columns,
    size_t rowCount,
    TemporaryColumns& temporaryColumns)
{
    const std::string prefix = std::string(functionName) + "(";
    size_t searchPosition = 0;
    size_t sequence = 0;
    while (true)
    {
        const size_t callStart = expression.find(prefix, searchPosition);
        if (callStart == std::string::npos)
            break;
        if (callStart > 0)
        {
            const char previous = expression[callStart - 1];
            if (std::isalnum(static_cast<unsigned char>(previous)) || previous == '_')
            {
                searchPosition = callStart + prefix.size();
                continue;
            }
        }

        const size_t argumentStart = callStart + prefix.size();
        size_t argumentEnd = argumentStart;
        int depth = 1;
        for (; argumentEnd < expression.size() && depth > 0; ++argumentEnd)
        {
            if (expression[argumentEnd] == '(')
                ++depth;
            else if (expression[argumentEnd] == ')')
                --depth;
        }
        if (depth != 0)
        {
            return {ExpressionEngineStatus::CompileError,
                    "Unclosed custom function call: " + std::string(functionName), {}};
        }

        const size_t closingParen = argumentEnd - 1;
        const std::string argument = trim(expression.substr(
            argumentStart, closingParen - argumentStart));
        if (!isIdentifier(argument))
        {
            return {ExpressionEngineStatus::CompileError,
                    std::string(functionName) + " expects one data-column name.", {}};
        }

        const auto column = columns.find(argument);
        if (column == columns.end())
        {
            return {ExpressionEngineStatus::MissingSymbol,
                    "The custom function references a missing data item.", {argument}};
        }
        if (!column->second.data || column->second.size != rowCount)
        {
            return {ExpressionEngineStatus::InvalidRequest,
                    "A custom-function input has an invalid row count.", {}};
        }

        std::string temporaryName = "tmp_" + std::string(functionName) + "_"
            + argument + "_" + std::to_string(sequence++);
        while (columns.count(temporaryName) || temporaryColumns.count(temporaryName))
            temporaryName += "_";

        auto& values = temporaryColumns[temporaryName];
        values.assign(rowCount, 0.0);
        const double* source = column->second.data;
        if (forward)
        {
            for (size_t row = 0; row + 1 < rowCount; ++row)
                values[row] = source[row + 1] - source[row];
        }
        else
        {
            for (size_t row = 1; row < rowCount; ++row)
                values[row] = source[row] - source[row - 1];
        }

        expression.replace(callStart, argumentEnd - callStart, temporaryName);
        searchPosition = callStart + temporaryName.size();
    }
    return {ExpressionEngineStatus::Success, {}, {}};
}

std::vector<std::string> collectIdentifiers(const std::string& expression)
{
    std::vector<std::string> identifiers;
    std::unordered_set<std::string> seen;
    const char* current = expression.data();
    const char* end = current + expression.size();
    while (current < end)
    {
        while (current < end
               && !std::isalpha(static_cast<unsigned char>(*current))
               && *current != '_')
        {
            ++current;
        }
        if (current == end)
            break;
        const char* start = current++;
        while (current < end
               && (std::isalnum(static_cast<unsigned char>(*current))
                   || *current == '_'))
        {
            ++current;
        }
        std::string identifier(start, current);
        if (seen.insert(identifier).second)
            identifiers.push_back(std::move(identifier));
    }
    return identifiers;
}

} // namespace

ExpressionEngineResult ExpressionEngine::evaluate(
    const std::string& expression,
    const std::vector<ExpressionColumnView>& columns,
    const std::vector<ExpressionScalarValue>& scalars,
    size_t rowCount,
    double* output,
    size_t outputSize)
{
    if (expression.empty() || rowCount == 0 || (output && outputSize != rowCount))
    {
        return {ExpressionEngineStatus::InvalidRequest,
                "The expression, row count, or output buffer is invalid.", {}};
    }

    ColumnMap columnMap;
    columnMap.reserve(columns.size());
    for (const auto& column : columns)
    {
        if (!column.data || column.size != rowCount
            || !columnMap.emplace(column.name, column).second)
        {
            return {ExpressionEngineStatus::InvalidRequest,
                    "The expression data source contains an invalid column.", {}};
        }
    }

    std::unordered_map<std::string, double> scalarMap;
    scalarMap.reserve(scalars.size());
    for (const auto& scalar : scalars)
    {
        if (!isIdentifier(scalar.name) || columnMap.count(scalar.name)
            || !scalarMap.emplace(scalar.name, scalar.value).second)
        {
            return {ExpressionEngineStatus::InvalidRequest,
                    "A scalar name is invalid, duplicated, or collides with a data item.", {}};
        }
    }

    std::string processed = expression;
    TemporaryColumns temporaryColumns;
    ExpressionEngineResult preprocessed = preprocessDifferenceFunction(
        processed, "fdiff", true, columnMap, rowCount, temporaryColumns);
    if (!preprocessed.success())
        return preprocessed;
    preprocessed = preprocessDifferenceFunction(
        processed, "bdiff", false, columnMap, rowCount, temporaryColumns);
    if (!preprocessed.success())
        return preprocessed;

    std::vector<std::string> references;
    std::vector<std::string> missing;
    const auto& keywords = GetExprtkKeywords();
    for (const std::string& identifier : collectIdentifiers(processed))
    {
        if (keywords.count(identifier))
            continue;
        if (columnMap.count(identifier) || temporaryColumns.count(identifier)
            || scalarMap.count(identifier))
        {
            references.push_back(identifier);
        }
        else
        {
            missing.push_back(identifier);
        }
    }
    if (!missing.empty())
    {
        return {ExpressionEngineStatus::MissingSymbol,
                "The expression references symbols that are not available.",
                std::move(missing)};
    }

    exprtk::symbol_table<double> symbolTable;
    std::vector<double> variables(references.size(), 0.0);
    for (size_t index = 0; index < references.size(); ++index)
    {
        if (!symbolTable.add_variable(references[index], variables[index]))
        {
            return {ExpressionEngineStatus::InvalidRequest,
                    "An expression symbol could not be registered.", {references[index]}};
        }
    }
    symbolTable.add_constants();

    exprtk::expression<double> compiledExpression;
    compiledExpression.register_symbol_table(symbolTable);
    exprtk::parser<double> parser;
    if (!parser.compile(processed, compiledExpression))
    {
        return {ExpressionEngineStatus::CompileError,
                parser.error().empty() ? "The expression could not be compiled."
                                       : parser.error(), {}};
    }
    if (!output)
        return {ExpressionEngineStatus::Success, {}, {}};

    for (size_t row = 0; row < rowCount; ++row)
    {
        for (size_t index = 0; index < references.size(); ++index)
        {
            const std::string& reference = references[index];
            const auto column = columnMap.find(reference);
            if (column != columnMap.end())
                variables[index] = column->second.data[row];
            else
            {
                const auto temporary = temporaryColumns.find(reference);
                if (temporary != temporaryColumns.end())
                    variables[index] = temporary->second[row];
                else
                    variables[index] = scalarMap.at(reference);
            }
        }
        output[row] = compiledExpression.value();
    }
    return {ExpressionEngineStatus::Success, {}, {}};
}

} // namespace viewer
