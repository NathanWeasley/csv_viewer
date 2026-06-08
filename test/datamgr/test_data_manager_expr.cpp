#include "test_case.h"
#include "code_viewer/datamgr/data_manager.h"
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

//#define PATH_PREFIX     "test/data/"
#define PATH_PREFIX     "../../data/"

namespace {

// Helper: get the file path relative to the test executable dir
std::string testDataPath(const std::string& filename)
{
    return std::string(TEST_PROJECT_ROOT) + "/test/data/" + filename;
}

} // anonymous namespace

TEST_GROUP(DataManagerExpr)
{

// ============================================================
// 基础算术表达式
// ============================================================
TEST(DataManagerExpr, SimpleArithmetic)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    // A + B
    TEST_ASSERT_TRUE(dm.LoadFromExpr("A + B", "sum"));

    // 验证结果列存在且类型正确
    const viewer::AbstractColumn* sumCol = dm.GetColumn("sum");
    TEST_ASSERT_TRUE(sumCol != nullptr);
    TEST_ASSERT_EQ(sumCol->type(), viewer::ColumnType::Float64);

    // 验证行数
    TEST_ASSERT_EQ(sumCol->size(), 3u);

    // 逐行验证: A=[1,2,3], B=[10,20,30] -> sum=[11,22,33]
    TEST_ASSERT_NEAR(sumCol->getDouble(0), 11.0, 1e-9);
    TEST_ASSERT_NEAR(sumCol->getDouble(1), 22.0, 1e-9);
    TEST_ASSERT_NEAR(sumCol->getDouble(2), 33.0, 1e-9);
}

// ============================================================
// 乘法表达式
// ============================================================
TEST(DataManagerExpr, Multiplication)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    TEST_ASSERT_TRUE(dm.LoadFromExpr("A * B", "prod"));

    const viewer::AbstractColumn* prodCol = dm.GetColumn("prod");
    TEST_ASSERT_TRUE(prodCol != nullptr);
    TEST_ASSERT_EQ(prodCol->size(), 3u);

    // 1*10=10, 2*20=40, 3*30=90
    TEST_ASSERT_NEAR(prodCol->getDouble(0), 10.0, 1e-9);
    TEST_ASSERT_NEAR(prodCol->getDouble(1), 40.0, 1e-9);
    TEST_ASSERT_NEAR(prodCol->getDouble(2), 90.0, 1e-9);
}

// ============================================================
// 常量表达式（不引用任何列）
// ============================================================
TEST(DataManagerExpr, ConstantExpr)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    TEST_ASSERT_TRUE(dm.LoadFromExpr("3.14 * 2", "constVal"));

    const viewer::AbstractColumn* col = dm.GetColumn("constVal");
    TEST_ASSERT_TRUE(col != nullptr);
    TEST_ASSERT_EQ(col->size(), 3u);

    // 每行都是 6.28
    TEST_ASSERT_NEAR(col->getDouble(0), 6.28, 1e-9);
    TEST_ASSERT_NEAR(col->getDouble(1), 6.28, 1e-9);
    TEST_ASSERT_NEAR(col->getDouble(2), 6.28, 1e-9);
}

// ============================================================
// 数学函数表达式
// ============================================================
TEST(DataManagerExpr, MathFunc)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    TEST_ASSERT_TRUE(dm.LoadFromExpr("sin(A) * 10 + sqrt(B)", "calc"));

    const viewer::AbstractColumn* col = dm.GetColumn("calc");
    TEST_ASSERT_TRUE(col != nullptr);
    TEST_ASSERT_EQ(col->size(), 3u);

    // row0: sin(1)*10 + sqrt(10) ≈ 8.4147 + 3.1623 = 11.5770
    TEST_ASSERT_NEAR(col->getDouble(0), std::sin(1.0) * 10.0 + std::sqrt(10.0), 1e-6);
    // row1: sin(2)*10 + sqrt(20) ≈ 9.0930 + 4.4721 = 13.5651
    TEST_ASSERT_NEAR(col->getDouble(1), std::sin(2.0) * 10.0 + std::sqrt(20.0), 1e-6);
    // row2: sin(3)*10 + sqrt(30) ≈ 1.4112 + 5.4772 = 6.8884
    TEST_ASSERT_NEAR(col->getDouble(2), std::sin(3.0) * 10.0 + std::sqrt(30.0), 1e-6);
}

// ============================================================
// 列名冲突
// ============================================================
TEST(DataManagerExpr, NameConflict)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    // A 列已存在，尝试用 "A" 作为表达式名
    TEST_ASSERT_FALSE(dm.LoadFromExpr("A + B", "A"));

    // 验证 A 列仍然是原始类型（Int64）
    const viewer::AbstractColumn* aCol = dm.GetColumn("A");
    TEST_ASSERT_TRUE(aCol != nullptr);
    TEST_ASSERT_EQ(aCol->type(), viewer::ColumnType::Int64);
}

// ============================================================
// 引用不存在的列
// ============================================================
TEST(DataManagerExpr, UnknownColumn)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    // X 列不存在
    TEST_ASSERT_FALSE(dm.LoadFromExpr("X + A", "bad"));

    // 验证 "bad" 没有注册
    TEST_ASSERT_TRUE(dm.GetColumn("bad") == nullptr);
}

// ============================================================
// 语法错误表达式
// ============================================================
TEST(DataManagerExpr, SyntaxError)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    // 不完整的表达式
    TEST_ASSERT_FALSE(dm.LoadFromExpr("A +", "bad"));
    TEST_ASSERT_TRUE(dm.GetColumn("bad") == nullptr);

    // 无效语法
    TEST_ASSERT_FALSE(dm.LoadFromExpr("A +* B", "bad2"));
    TEST_ASSERT_TRUE(dm.GetColumn("bad2") == nullptr);
}

// ============================================================
// 链式表达式引用
// ============================================================
TEST(DataManagerExpr, Chaining)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    // 第一步: total = A + B (避免 exprtk 内置 sum 函数名冲突)
    TEST_ASSERT_TRUE(dm.LoadFromExpr("A + B", "total"));

    // 第二步: dbl = total * 2
    TEST_ASSERT_TRUE(dm.LoadFromExpr("total * 2", "dbl"));

    const viewer::AbstractColumn* dblCol = dm.GetColumn("dbl");
    TEST_ASSERT_TRUE(dblCol != nullptr);
    TEST_ASSERT_EQ(dblCol->size(), 3u);

    // total=[11,22,33], dbl=[22,44,66]
    TEST_ASSERT_NEAR(dblCol->getDouble(0), 22.0, 1e-9);
    TEST_ASSERT_NEAR(dblCol->getDouble(1), 44.0, 1e-9);
    TEST_ASSERT_NEAR(dblCol->getDouble(2), 66.0, 1e-9);

    // 验证列总数: A, B, total, dbl = 4
    TEST_ASSERT_EQ(dm.GetColumnCount(), 4u);
}

// ============================================================
// 多 CSV 加载后表达式
// ============================================================
TEST(DataManagerExpr, MultiCSV)
{
    viewer::DataManager dm;

    // 加载第一个 CSV (3行)
    viewer::LoadConfig cfg1;
    cfg1.filePath = testDataPath("test_basic.csv");
    cfg1.hasHeader = true;
    cfg1.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg1));
    TEST_ASSERT_EQ(dm.GetRowCount(), 3u);

    // 追加第二个 CSV (2行，相同列名)
    viewer::LoadConfig cfg2;
    cfg2.filePath = testDataPath("test_extra.csv");
    cfg2.hasHeader = true;
    cfg2.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg2));

    // 现在应该有 5 行数据
    TEST_ASSERT_EQ(dm.GetRowCount(), 5u);

    // 表达式计算: sum = A + B，应对所有 5 行生效
    TEST_ASSERT_TRUE(dm.LoadFromExpr("A + B", "sum"));

    const viewer::AbstractColumn* sumCol = dm.GetColumn("sum");
    TEST_ASSERT_TRUE(sumCol != nullptr);
    TEST_ASSERT_EQ(sumCol->size(), 5u);

    // 前 3 行: A+B
    // row0: 1+10=11
    // row1: 2+20=22
    // row2: 3+30=33
    TEST_ASSERT_NEAR(sumCol->getDouble(0), 11.0, 1e-9);
    TEST_ASSERT_NEAR(sumCol->getDouble(1), 22.0, 1e-9);
    TEST_ASSERT_NEAR(sumCol->getDouble(2), 33.0, 1e-9);

    // 后 2 行: 4+40=44, 5+50=55
    TEST_ASSERT_NEAR(sumCol->getDouble(3), 44.0, 1e-9);
    TEST_ASSERT_NEAR(sumCol->getDouble(4), 55.0, 1e-9);
}

// ============================================================
// 追加 CSV 列名不匹配
// ============================================================
TEST(DataManagerExpr, ColumnNameMismatch)
{
    viewer::DataManager dm;

    // 加载 A,B 两列的 CSV
    viewer::LoadConfig cfg1;
    cfg1.filePath = testDataPath("test_basic.csv");
    cfg1.hasHeader = true;
    cfg1.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg1));

    // 加载 X 单列的 CSV，列名不匹配
    viewer::LoadConfig cfg2;
    cfg2.filePath = testDataPath("test_single.csv");
    cfg2.hasHeader = true;
    cfg2.headerRow = 0;

    TEST_ASSERT_FALSE(dm.LoadFromCSV(cfg2));

    // 数据应保持不变
    TEST_ASSERT_EQ(dm.GetRowCount(), 3u);
    TEST_ASSERT_EQ(dm.GetColumnCount(), 2u);
}

// ============================================================
// 空表达式
// ============================================================
TEST(DataManagerExpr, EmptyExpr)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    TEST_ASSERT_FALSE(dm.LoadFromExpr("", "empty"));
    TEST_ASSERT_TRUE(dm.GetColumn("empty") == nullptr);
}

// ============================================================
// 空 DataManager 加载表达式
// ============================================================
TEST(DataManagerExpr, ExprBeforeCSV)
{
    viewer::DataManager dm;

    // 没有加载任何 CSV，但 LoadingFromExpr 常量表达式应该成功
    // 此时 rowCount = 0，结果列为空
    TEST_ASSERT_TRUE(dm.LoadFromExpr("3.14", "piVal"));

    const viewer::AbstractColumn* col = dm.GetColumn("piVal");
    TEST_ASSERT_TRUE(col != nullptr);
    TEST_ASSERT_EQ(col->size(), 0u);
    TEST_ASSERT_EQ(col->type(), viewer::ColumnType::Float64);
}

// ============================================================
// 标量加法: A + 5
// ============================================================
TEST(DataManagerExpr, ScalarAdd)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    TEST_ASSERT_TRUE(dm.LoadFromExpr("A + 5", "add5"));

    const viewer::AbstractColumn* col = dm.GetColumn("add5");
    TEST_ASSERT_TRUE(col != nullptr);
    TEST_ASSERT_EQ(col->size(), 3u);

    TEST_ASSERT_NEAR(col->getDouble(0), 6.0, 1e-9);   // 1+5
    TEST_ASSERT_NEAR(col->getDouble(1), 7.0, 1e-9);   // 2+5
    TEST_ASSERT_NEAR(col->getDouble(2), 8.0, 1e-9);   // 3+5
}

// ============================================================
// 标量减法: B - 10
// ============================================================
TEST(DataManagerExpr, ScalarSubtract)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    TEST_ASSERT_TRUE(dm.LoadFromExpr("B - 10", "sub10"));

    const viewer::AbstractColumn* col = dm.GetColumn("sub10");
    TEST_ASSERT_TRUE(col != nullptr);
    TEST_ASSERT_EQ(col->size(), 3u);

    TEST_ASSERT_NEAR(col->getDouble(0), 0.0, 1e-9);   // 10-10
    TEST_ASSERT_NEAR(col->getDouble(1), 10.0, 1e-9);  // 20-10
    TEST_ASSERT_NEAR(col->getDouble(2), 20.0, 1e-9);  // 30-10
}

// ============================================================
// 标量乘法: A * 2.5
// ============================================================
TEST(DataManagerExpr, ScalarMultiply)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    TEST_ASSERT_TRUE(dm.LoadFromExpr("A * 2.5", "mul"));

    const viewer::AbstractColumn* col = dm.GetColumn("mul");
    TEST_ASSERT_TRUE(col != nullptr);
    TEST_ASSERT_EQ(col->size(), 3u);

    TEST_ASSERT_NEAR(col->getDouble(0), 2.5, 1e-9);   // 1*2.5
    TEST_ASSERT_NEAR(col->getDouble(1), 5.0, 1e-9);   // 2*2.5
    TEST_ASSERT_NEAR(col->getDouble(2), 7.5, 1e-9);   // 3*2.5
}

// ============================================================
// 标量除法: B / 5
// ============================================================
TEST(DataManagerExpr, ScalarDivide)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    TEST_ASSERT_TRUE(dm.LoadFromExpr("B / 5", "div"));

    const viewer::AbstractColumn* col = dm.GetColumn("div");
    TEST_ASSERT_TRUE(col != nullptr);
    TEST_ASSERT_EQ(col->size(), 3u);

    TEST_ASSERT_NEAR(col->getDouble(0), 2.0, 1e-9);   // 10/5
    TEST_ASSERT_NEAR(col->getDouble(1), 4.0, 1e-9);   // 20/5
    TEST_ASSERT_NEAR(col->getDouble(2), 6.0, 1e-9);   // 30/5
}

// ============================================================
// 标量与数学函数混合: sin(A) + 0.5
// ============================================================
TEST(DataManagerExpr, ScalarWithFunc)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    TEST_ASSERT_TRUE(dm.LoadFromExpr("sin(A) + 0.5", "sin_offset"));

    const viewer::AbstractColumn* col = dm.GetColumn("sin_offset");
    TEST_ASSERT_TRUE(col != nullptr);
    TEST_ASSERT_EQ(col->size(), 3u);

    TEST_ASSERT_NEAR(col->getDouble(0), std::sin(1.0) + 0.5, 1e-9);
    TEST_ASSERT_NEAR(col->getDouble(1), std::sin(2.0) + 0.5, 1e-9);
    TEST_ASSERT_NEAR(col->getDouble(2), std::sin(3.0) + 0.5, 1e-9);
}

// ============================================================
// 链式标量: a1 = A + 1, a2 = a1 * 2
// ============================================================
TEST(DataManagerExpr, ChainedScalar)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    TEST_ASSERT_TRUE(dm.LoadFromExpr("A + 1", "a1"));
    TEST_ASSERT_TRUE(dm.LoadFromExpr("a1 * 2", "a2"));

    const viewer::AbstractColumn* col = dm.GetColumn("a2");
    TEST_ASSERT_TRUE(col != nullptr);
    TEST_ASSERT_EQ(col->size(), 3u);

    // A=[1,2,3], a1=[2,3,4], a2=[4,6,8]
    TEST_ASSERT_NEAR(col->getDouble(0), 4.0, 1e-9);
    TEST_ASSERT_NEAR(col->getDouble(1), 6.0, 1e-9);
    TEST_ASSERT_NEAR(col->getDouble(2), 8.0, 1e-9);
}

// ============================================================
// 科学计数法: A * 1.1e4
// ============================================================
TEST(DataManagerExpr, ScientificNotation)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    TEST_ASSERT_TRUE(dm.LoadFromExpr("A * 1.1e4", "sci"));

    const viewer::AbstractColumn* col = dm.GetColumn("sci");
    TEST_ASSERT_TRUE(col != nullptr);
    TEST_ASSERT_EQ(col->size(), 3u);

    TEST_ASSERT_NEAR(col->getDouble(0), 11000.0, 1e-9);  // 1 * 11000
    TEST_ASSERT_NEAR(col->getDouble(1), 22000.0, 1e-9);  // 2 * 11000
    TEST_ASSERT_NEAR(col->getDouble(2), 33000.0, 1e-9);  // 3 * 11000
}

// ============================================================
// 科学计数法负指数: A + 2.5e-3
// ============================================================
TEST(DataManagerExpr, ScientificMixed)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    TEST_ASSERT_TRUE(dm.LoadFromExpr("A + 2.5e-3", "sci_neg"));

    const viewer::AbstractColumn* col = dm.GetColumn("sci_neg");
    TEST_ASSERT_TRUE(col != nullptr);
    TEST_ASSERT_EQ(col->size(), 3u);

    TEST_ASSERT_NEAR(col->getDouble(0), 1.0025, 1e-9);
    TEST_ASSERT_NEAR(col->getDouble(1), 2.0025, 1e-9);
    TEST_ASSERT_NEAR(col->getDouble(2), 3.0025, 1e-9);
}

// ============================================================
// 圆周率 pi: A * pi
// ============================================================
TEST(DataManagerExpr, PiLiteral)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    TEST_ASSERT_TRUE(dm.LoadFromExpr("A * pi", "pi_mul"));

    const viewer::AbstractColumn* col = dm.GetColumn("pi_mul");
    TEST_ASSERT_TRUE(col != nullptr);
    TEST_ASSERT_EQ(col->size(), 3u);

    const double pi = 3.141592653589793;
    TEST_ASSERT_NEAR(col->getDouble(0), 1.0 * pi, 1e-9);
    TEST_ASSERT_NEAR(col->getDouble(1), 2.0 * pi, 1e-9);
    TEST_ASSERT_NEAR(col->getDouble(2), 3.0 * pi, 1e-9);
}

// ============================================================
// 圆周率 pi 与函数: sin(pi / 2) → 1.0
// ============================================================
TEST(DataManagerExpr, PiWithFunc)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    TEST_ASSERT_TRUE(dm.LoadFromExpr("sin(pi / 2)", "sin_pi"));

    const viewer::AbstractColumn* col = dm.GetColumn("sin_pi");
    TEST_ASSERT_TRUE(col != nullptr);
    TEST_ASSERT_EQ(col->size(), 3u);

    // sin(pi/2) = 1.0 for all rows
    TEST_ASSERT_NEAR(col->getDouble(0), 1.0, 1e-9);
    TEST_ASSERT_NEAR(col->getDouble(1), 1.0, 1e-9);
    TEST_ASSERT_NEAR(col->getDouble(2), 1.0, 1e-9);
}

// ============================================================
// 前向差分 fdiff
// ============================================================
TEST(DataManagerExpr, FdiffBasic)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    // fdiff(A): A=[1,2,3] → [1,1,0]
    TEST_ASSERT_TRUE(dm.LoadFromExpr("fdiff(A)", "fd"));

    const viewer::AbstractColumn* col = dm.GetColumn("fd");
    TEST_ASSERT_TRUE(col != nullptr);
    TEST_ASSERT_EQ(col->size(), 3u);
    TEST_ASSERT_EQ(col->type(), viewer::ColumnType::Float64);

    TEST_ASSERT_NEAR(col->getDouble(0), 1.0, 1e-9);  // 2-1
    TEST_ASSERT_NEAR(col->getDouble(1), 1.0, 1e-9);  // 3-2
    TEST_ASSERT_NEAR(col->getDouble(2), 0.0, 1e-9);  // last row
}

// ============================================================
// fdiff 与表达式混合
// ============================================================
TEST(DataManagerExpr, FdiffInExpr)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    // fdiff(A) * 2 → [2,2,0]
    TEST_ASSERT_TRUE(dm.LoadFromExpr("fdiff(A) * 2", "fd2"));

    const viewer::AbstractColumn* col = dm.GetColumn("fd2");
    TEST_ASSERT_TRUE(col != nullptr);
    TEST_ASSERT_EQ(col->size(), 3u);

    TEST_ASSERT_NEAR(col->getDouble(0), 2.0, 1e-9);
    TEST_ASSERT_NEAR(col->getDouble(1), 2.0, 1e-9);
    TEST_ASSERT_NEAR(col->getDouble(2), 0.0, 1e-9);
}

// ============================================================
// 后向差分 bdiff
// ============================================================
TEST(DataManagerExpr, BdiffBasic)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    // bdiff(A): A=[1,2,3] → [0,1,1]
    TEST_ASSERT_TRUE(dm.LoadFromExpr("bdiff(A)", "bd"));

    const viewer::AbstractColumn* col = dm.GetColumn("bd");
    TEST_ASSERT_TRUE(col != nullptr);
    TEST_ASSERT_EQ(col->size(), 3u);
    TEST_ASSERT_EQ(col->type(), viewer::ColumnType::Float64);

    TEST_ASSERT_NEAR(col->getDouble(0), 0.0, 1e-9);  // first row
    TEST_ASSERT_NEAR(col->getDouble(1), 1.0, 1e-9);  // 2-1
    TEST_ASSERT_NEAR(col->getDouble(2), 1.0, 1e-9);  // 3-2
}

// ============================================================
// bdiff 与表达式混合
// ============================================================
TEST(DataManagerExpr, BdiffInExpr)
{
    viewer::DataManager dm;

    viewer::LoadConfig cfg;
    cfg.filePath = testDataPath("test_basic.csv");
    cfg.hasHeader = true;
    cfg.headerRow = 0;

    TEST_ASSERT_TRUE(dm.LoadFromCSV(cfg));

    // bdiff(B) + A: bdiff(B)=[0,10,10], A=[1,2,3] → [1,12,13]
    TEST_ASSERT_TRUE(dm.LoadFromExpr("bdiff(B) + A", "bd_a"));

    const viewer::AbstractColumn* col = dm.GetColumn("bd_a");
    TEST_ASSERT_TRUE(col != nullptr);
    TEST_ASSERT_EQ(col->size(), 3u);

    TEST_ASSERT_NEAR(col->getDouble(0), 1.0, 1e-9);   // 0+1
    TEST_ASSERT_NEAR(col->getDouble(1), 12.0, 1e-9);  // (20-10)+2
    TEST_ASSERT_NEAR(col->getDouble(2), 13.0, 1e-9);  // (30-20)+3
}

} // TEST_GROUP(DataManagerExpr)
