#include "test_case.h"
#include "code_viewer/datamgr/expression_engine.h"

#include <vector>

TEST_GROUP(ExpressionEngine)
{

TEST(ExpressionEngine, UsesViewerColumnsScalarsAndDifferenceFunctions)
{
    const std::vector<double> source{1.0, 4.0, 9.0};
    const std::vector<viewer::ExpressionColumnView> columns{
        {"source", source.data(), source.size()}
    };
    const std::vector<viewer::ExpressionScalarValue> scalars{
        {"gain", 2.0}
    };
    std::vector<double> output(source.size());
    const viewer::ExpressionEngineResult result = viewer::ExpressionEngine::evaluate(
        "fdiff(source) + gain", columns, scalars,
        source.size(), output.data(), output.size());

    TEST_ASSERT_TRUE(result.success());
    TEST_ASSERT_NEAR(output[0], 5.0, 1e-12);
    TEST_ASSERT_NEAR(output[1], 7.0, 1e-12);
    TEST_ASSERT_NEAR(output[2], 2.0, 1e-12);
}

TEST(ExpressionEngine, ReportsMissingSymbols)
{
    const std::vector<double> source{1.0, 2.0};
    const std::vector<viewer::ExpressionColumnView> columns{
        {"source", source.data(), source.size()}
    };
    std::vector<double> output(source.size());
    const viewer::ExpressionEngineResult result = viewer::ExpressionEngine::evaluate(
        "source + missing_value", columns, {},
        source.size(), output.data(), output.size());

    TEST_ASSERT_FALSE(result.success());
    TEST_ASSERT_EQ(result.status, viewer::ExpressionEngineStatus::MissingSymbol);
    TEST_ASSERT_EQ(result.missingSymbols.size(), 1u);
    TEST_ASSERT_EQ(result.missingSymbols.front(), std::string("missing_value"));
}

TEST(ExpressionEngine, IgnoresUnreferencedNonIdentifierColumnNames)
{
    const std::vector<double> source{2.0, 3.0};
    const std::vector<double> displayOnly{8.0, 9.0};
    const std::vector<viewer::ExpressionColumnView> columns{
        {"source", source.data(), source.size()},
        {"joint/position[0]", displayOnly.data(), displayOnly.size()}
    };
    std::vector<double> output(source.size());
    const viewer::ExpressionEngineResult result = viewer::ExpressionEngine::evaluate(
        "source * 2", columns, {}, source.size(), output.data(), output.size());

    TEST_ASSERT_TRUE(result.success());
    TEST_ASSERT_NEAR(output[0], 4.0, 1e-12);
    TEST_ASSERT_NEAR(output[1], 6.0, 1e-12);
}

} // TEST_GROUP(ExpressionEngine)
