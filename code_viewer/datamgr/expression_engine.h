#pragma once

#include "code_viewer/base/base_def.h"

#include <cstddef>
#include <string>
#include <vector>

namespace viewer
{

struct ExpressionColumnView
{
    std::string name;
    const double* data = nullptr;
    size_t size = 0;
};

struct ExpressionScalarValue
{
    std::string name;
    double value = 0.0;
};

enum class ExpressionEngineStatus
{
    Success,
    InvalidRequest,
    MissingSymbol,
    CompileError,
    EvaluationError
};

struct ExpressionEngineResult
{
    ExpressionEngineStatus status = ExpressionEngineStatus::InvalidRequest;
    std::string error;
    std::vector<std::string> missingSymbols;

    bool success() const noexcept
    {
        return status == ExpressionEngineStatus::Success;
    }
};

class VIEWER_API ExpressionEngine
{
public:
    // output == nullptr performs validation only. Otherwise outputSize must be
    // identical to the row count of every supplied column.
    static ExpressionEngineResult evaluate(
        const std::string& expression,
        const std::vector<ExpressionColumnView>& columns,
        const std::vector<ExpressionScalarValue>& scalars,
        size_t rowCount,
        double* output,
        size_t outputSize);
};

} // namespace viewer
