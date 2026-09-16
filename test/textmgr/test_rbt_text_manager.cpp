#include "test_case.h"

#include "code_viewer/textmgr/rbt_text_manager.h"

#include <QFile>
#include <QTemporaryDir>

#include <utility>

namespace
{

QString writeLog(QTemporaryDir& directory, const QByteArray& content)
{
    const QString path = directory.filePath(QStringLiteral("sample.log"));
    QFile file(path);
    TEST_ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    TEST_ASSERT_EQ(file.write(content), content.size());
    file.close();
    return path;
}

viewer::RbtPatternRule makeRule(
    QString id, QString name, QString expression, QColor color,
    viewer::RbtPatternMatchType matchType =
        viewer::RbtPatternMatchType::RegularExpression)
{
    viewer::RbtPatternRule rule;
    rule.id = std::move(id);
    rule.name = std::move(name);
    rule.expression = std::move(expression);
    rule.matchType = matchType;
    rule.color = std::move(color);
    return rule;
}

} // namespace

TEST_GROUP(RbtTextManager)
{

TEST(RbtTextManager, NavigatesSparseAndDenseMatchSets)
{
    viewer::RbtMatchSet sparse({3, 200}, 1000);
    TEST_ASSERT_EQ(sparse.count(), 2);
    TEST_ASSERT_EQ(sparse.first(), 3);
    TEST_ASSERT_EQ(sparse.next(-1), 3);
    TEST_ASSERT_EQ(sparse.next(3), 200);
    TEST_ASSERT_EQ(sparse.previous(200), 3);
    TEST_ASSERT_EQ(sparse.last(), 200);

    viewer::RbtMatchSet dense({1, 5, 40, 99}, 100);
    TEST_ASSERT_TRUE(dense.contains(40));
    TEST_ASSERT_FALSE(dense.contains(41));
    TEST_ASSERT_EQ(dense.next(-1), 1);
    TEST_ASSERT_EQ(dense.next(5), 40);
    TEST_ASSERT_EQ(dense.previous(99), 40);
}

TEST(RbtTextManager, MapsUtf8AndRemovesCrLfFromLines)
{
    QTemporaryDir directory;
    TEST_ASSERT_TRUE(directory.isValid());
    const QByteArray content = QByteArray("first\r\n")
        + QByteArray::fromHex("e99499e8afaf") + " ERROR\r\nlast";
    const QString path = writeLog(directory, content);
    QString error;
    const auto document = viewer::RbtTextDocument::open(path, &error);
    TEST_ASSERT_TRUE(document != nullptr);
    TEST_ASSERT_TRUE(error.isEmpty());
    TEST_ASSERT_EQ(document->lineCount(), 3);
    TEST_ASSERT_TRUE(document->lineText(0) == QStringLiteral("first"));
    TEST_ASSERT_TRUE(document->lineText(1) == QString::fromUtf8(u8"错误 ERROR"));

    qsizetype line = -1;
    qsizetype column = -1;
    qsizetype length = -1;
    TEST_ASSERT_TRUE(document->textPositionForByteOffset(
        14, 5, &line, &column, &length));
    TEST_ASSERT_EQ(line, 1);
    TEST_ASSERT_EQ(column, 3);
    TEST_ASSERT_EQ(length, 5);
}

TEST(RbtTextManager, FindsCaseInsensitiveTextAndWraps)
{
    QTemporaryDir directory;
    TEST_ASSERT_TRUE(directory.isValid());
    const QString path = writeLog(directory, "Alpha\nERROR\nomega\n");

    const viewer::RbtFindResult first = viewer::RbtTextSearcher::find(
        path, "error", 0, false, false);
    TEST_ASSERT_EQ(first.offset, 6);
    TEST_ASSERT_EQ(first.byteLength, 5);
    TEST_ASSERT_FALSE(first.wrapped);

    const viewer::RbtFindResult wrapped = viewer::RbtTextSearcher::find(
        path, "Alpha", 7, false, true);
    TEST_ASSERT_EQ(wrapped.offset, 0);
    TEST_ASSERT_TRUE(wrapped.wrapped);
}

TEST(RbtTextManager, CompilesExactRegexAndFuzzyMatchTypes)
{
    viewer::RbtPatternRule rule = makeRule(
        QStringLiteral("match"), QStringLiteral("match"),
        QStringLiteral("hello world"), QColor(255, 0, 0, 72));

    rule.matchType = viewer::RbtPatternMatchType::Exact;
    QRegularExpression expression = viewer::RbtTextManager::compilePattern(rule);
    TEST_ASSERT_TRUE(expression.isValid());
    TEST_ASSERT_TRUE(expression.match(QStringLiteral("hello world")).hasMatch());
    TEST_ASSERT_FALSE(expression.match(QStringLiteral("prefix hello world")).hasMatch());
    TEST_ASSERT_FALSE(expression.match(QStringLiteral("HELLO WORLD")).hasMatch());

    rule.matchType = viewer::RbtPatternMatchType::RegularExpression;
    rule.expression = QStringLiteral("call\\s+system\\s+func");
    expression = viewer::RbtTextManager::compilePattern(rule);
    TEST_ASSERT_TRUE(expression.match(QStringLiteral("call system func")).hasMatch());
    rule.expression = QStringLiteral("/call system func/");
    expression = viewer::RbtTextManager::compilePattern(rule);
    TEST_ASSERT_FALSE(expression.match(QStringLiteral("call system func")).hasMatch());

    rule.matchType = viewer::RbtPatternMatchType::Fuzzy;
    rule.expression = QStringLiteral("hello world");
    expression = viewer::RbtTextManager::compilePattern(rule);
    TEST_ASSERT_TRUE(expression.isValid());
    TEST_ASSERT_TRUE(expression.match(QStringLiteral("HELLOWORLD")).hasMatch());
    TEST_ASSERT_TRUE(expression.match(QStringLiteral("hello   123 world")).hasMatch());
    TEST_ASSERT_TRUE(expression.match(QStringLiteral("hello wworldd")).hasMatch());
    TEST_ASSERT_FALSE(expression.match(QStringLiteral("hell123o world")).hasMatch());
    TEST_ASSERT_FALSE(expression.match(QStringLiteral("hello\nworld")).hasMatch());
}

TEST(RbtTextManager, ValidatesAndPersistsPatterns)
{
    QVector<viewer::RbtPatternRule> rules;
    rules.push_back(makeRule(QStringLiteral("error"), QStringLiteral("错误"),
                             QStringLiteral("ERROR|FAIL"), QColor(255, 0, 0, 72)));
    rules.push_back(makeRule(QStringLiteral("warn"), QStringLiteral("警告"),
                             QStringLiteral("WARN"), QColor(255, 193, 7, 80),
                             viewer::RbtPatternMatchType::Fuzzy));
    int errorRow = -1;
    QString error;
    TEST_ASSERT_TRUE(viewer::RbtTextManager::validatePatterns(
        rules, &errorRow, &error));

    QTemporaryDir directory;
    TEST_ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("patterns.json"));
    TEST_ASSERT_TRUE(viewer::RbtPatternRepository::save(path, rules, &error));
    QVector<viewer::RbtPatternRule> loaded;
    TEST_ASSERT_TRUE(viewer::RbtPatternRepository::load(path, &loaded, &error));
    TEST_ASSERT_EQ(loaded.size(), 2);
    TEST_ASSERT_TRUE(loaded[0].name == rules[0].name);
    TEST_ASSERT_TRUE(loaded[0].expression == rules[0].expression);
    TEST_ASSERT_TRUE(loaded[0].matchType == rules[0].matchType);
    TEST_ASSERT_TRUE(loaded[0].color == rules[0].color);
    TEST_ASSERT_TRUE(loaded[1].matchType == viewer::RbtPatternMatchType::Fuzzy);

    loaded[1].name = loaded[0].name;
    TEST_ASSERT_FALSE(viewer::RbtTextManager::validatePatterns(
        loaded, &errorRow, &error));
    TEST_ASSERT_EQ(errorRow, 1);
    TEST_ASSERT_FALSE(error.isEmpty());
}

TEST(RbtTextManager, LoadsLegacyPatternsAsRegularExpressions)
{
    QTemporaryDir directory;
    TEST_ASSERT_TRUE(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("legacy-patterns.json"));
    QFile file(path);
    TEST_ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    const QByteArray json = R"({
        "version": 1,
        "patterns": [{
            "id": "legacy",
            "name": "Legacy",
            "expression": "ERROR|FAIL",
            "color": "#50ff0000"
        }]
    })";
    TEST_ASSERT_EQ(file.write(json), json.size());
    file.close();

    QVector<viewer::RbtPatternRule> loaded;
    QString error;
    TEST_ASSERT_TRUE(viewer::RbtPatternRepository::load(path, &loaded, &error));
    TEST_ASSERT_EQ(loaded.size(), 1);
    TEST_ASSERT_TRUE(loaded[0].matchType
        == viewer::RbtPatternMatchType::RegularExpression);
}

} // TEST_GROUP(RbtTextManager)
