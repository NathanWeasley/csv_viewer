#pragma once

#include <string>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <vector>

namespace test
{

// ============================================================
// TestFailure: test failure exception
// ============================================================
struct TestFailure : public std::runtime_error
{
    explicit TestFailure(const std::string& msg)
        : std::runtime_error(msg)
    {}
};

// ============================================================
// TestResult: a single test record
// ============================================================
struct TestResult
{
    std::string groupName;
    std::string testName;
    bool        passed;
    std::string detail;
};

// ============================================================
// TestCase: abstract base class
// ============================================================
class TestCase
{
public:
    TestCase(std::string group, std::string name)
        : m_group(std::move(group))
        , m_name(std::move(name))
    {}

    virtual ~TestCase() = default;

    const std::string& group() const { return m_group; }
    const std::string& name()  const { return m_name; }

    virtual void run() = 0;

private:
    std::string m_group;
    std::string m_name;
};

} // namespace test

// ============================================================
// Assertion macros
// ============================================================

#define TEST_ASSERT_TRUE(cond)                                              \
    do {                                                                    \
        if (!(cond)) {                                                      \
            std::ostringstream __os;                                        \
            __os << "ASSERT_TRUE failed at " << __FILE__ << ":" << __LINE__ \
                 << "\n  Expression: " << #cond;                            \
            throw test::TestFailure(__os.str());                            \
        }                                                                   \
    } while(0)

#define TEST_ASSERT_FALSE(cond)                                             \
    do {                                                                    \
        if ((cond)) {                                                       \
            std::ostringstream __os;                                        \
            __os << "ASSERT_FALSE failed at " << __FILE__ << ":" << __LINE__\
                 << "\n  Expression: " << #cond;                            \
            throw test::TestFailure(__os.str());                            \
        }                                                                   \
    } while(0)

#define TEST_ASSERT_EQ(a, b)                                                \
    do {                                                                    \
        if (!((a) == (b))) {                                                \
            std::ostringstream __os;                                        \
            __os << "ASSERT_EQ failed at " << __FILE__ << ":" << __LINE__   \
                 << "\n  " << #a << " == " << #b;                           \
            throw test::TestFailure(__os.str());                            \
        }                                                                   \
    } while(0)

#define TEST_ASSERT_NE(a, b)                                                \
    do {                                                                    \
        if (!((a) != (b))) {                                                \
            std::ostringstream __os;                                        \
            __os << "ASSERT_NE failed at " << __FILE__ << ":" << __LINE__   \
                 << "\n  " << #a << " != " << #b;                           \
            throw test::TestFailure(__os.str());                            \
        }                                                                   \
    } while(0)

#define TEST_ASSERT_NEAR(a, b, eps)                                         \
    do {                                                                    \
        auto __a = (a);                                                     \
        auto __b = (b);                                                     \
        auto __eps = (eps);                                                 \
        if (std::abs(__a - __b) > __eps) {                                  \
            std::ostringstream __os;                                        \
            __os << "ASSERT_NEAR failed at " << __FILE__ << ":" << __LINE__ \
                 << "\n  Left:  " << __a                                    \
                 << "\n  Right: " << __b                                    \
                 << "\n  Epsilon: " << __eps;                               \
            throw test::TestFailure(__os.str());                            \
        }                                                                   \
    } while(0)

#define TEST_ASSERT_THROW(expr, exc_type)                                   \
    do {                                                                    \
        bool __caught = false;                                              \
        try { expr; }                                                       \
        catch (const exc_type&) { __caught = true; }                        \
        if (!__caught) {                                                    \
            std::ostringstream __os;                                        \
            __os << "ASSERT_THROW failed at " << __FILE__ << ":" << __LINE__\
                 << "\n  Expression: " << #expr                             \
                 << "\n  Expected exception: " << #exc_type;                \
            throw test::TestFailure(__os.str());                            \
        }                                                                   \
    } while(0)

// ============================================================
// Registration macros: TEST_GROUP + TEST
// ============================================================
#define TEST_GROUP(GROUP)                                                   \
    namespace /* test_group_ */

#define TEST(GROUP, NAME)                                                   \
    class Test_##GROUP##_##NAME : public ::test::TestCase {                 \
    public:                                                                 \
        Test_##GROUP##_##NAME() : TestCase(#GROUP, #NAME) {}                \
        void run() override;                                                \
    };                                                                      \
    static ::test::detail::TestRegistrar<Test_##GROUP##_##NAME>             \
        s_reg_##GROUP##_##NAME;                                             \
    void Test_##GROUP##_##NAME::run()

namespace test {
namespace detail {

// Simple global test registry
struct TestRegistry
{
    struct Entry
    {
        std::string group;
        std::string name;
        std::function<void()> testFunc;
    };

    std::vector<Entry> entries;

    static TestRegistry& instance()
    {
        static TestRegistry reg;
        return reg;
    }
};

inline void registerTest(const std::string& group,
                         const std::string& name,
                         std::function<void()> testFunc)
{
    TestRegistry::instance().entries.push_back({group, name, testFunc});
}

inline const std::vector<TestRegistry::Entry>& getAllTests()
{
    return TestRegistry::instance().entries;
}

// Helper registration template
template <typename T>
struct TestRegistrar
{
    TestRegistrar()
    {
        T instance;
        registerTest(instance.group(), instance.name(), [] { T().run(); });
    }
};

} // namespace detail
} // namespace test
