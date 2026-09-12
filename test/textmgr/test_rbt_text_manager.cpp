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
    QString id, QString name, QString expression, QColor color)
{
    viewer::RbtPatternRule rule;
    rule.id = std::move(id);
    rule.name = std::move(name);
    rule.expression = std::move(expression);
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

TEST(RbtTextManager, ValidatesAndPersistsPatterns)
{
    QVector<viewer::RbtPatternRule> rules;
    rules.push_back(makeRule(QStringLiteral("error"), QStringLiteral("错误"),
                             QStringLiteral("ERROR|FAIL"), QColor(255, 0, 0, 72)));
    rules.push_back(makeRule(QStringLiteral("warn"), QStringLiteral("警告"),
                             QStringLiteral("WARN"), QColor(255, 193, 7, 80)));
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
    TEST_ASSERT_TRUE(loaded[0].color == rules[0].color);

    loaded[1].name = loaded[0].name;
    TEST_ASSERT_FALSE(viewer::RbtTextManager::validatePatterns(
        loaded, &errorRow, &error));
    TEST_ASSERT_EQ(errorRow, 1);
    TEST_ASSERT_FALSE(error.isEmpty());
}

} // TEST_GROUP(RbtTextManager)
