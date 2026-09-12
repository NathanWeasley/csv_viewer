#pragma once

#include "code_viewer/base/base_def.h"

#include <QBitArray>
#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QObject>
#include <QString>
#include <QVector>

#include <atomic>
#include <memory>

namespace viewer
{

struct VIEWER_API RbtPatternRule
{
    QString id;
    QString name;
    QString expression;
    QColor color = QColor(255, 235, 59, 80);
};

class VIEWER_API RbtMatchSet final
{
public:
    RbtMatchSet() = default;
    RbtMatchSet(QVector<quint32> sortedLines, qsizetype totalLineCount);

    qsizetype count() const noexcept { return m_count; }
    bool contains(qsizetype line) const;
    qsizetype first() const;
    qsizetype last() const;
    qsizetype next(qsizetype line) const;
    qsizetype previous(qsizetype line) const;

private:
    QVector<quint32> m_sparseLines;
    QBitArray m_denseLines;
    qsizetype m_count = 0;
};

class VIEWER_API RbtMatchIndex final
{
public:
    RbtMatchIndex() = default;
    RbtMatchIndex(QString filePath,
                  quint64 documentGeneration,
                  quint64 ruleRevision,
                  QVector<RbtPatternRule> rules,
                  QVector<RbtMatchSet> matches);

    const QString& filePath() const noexcept { return m_filePath; }
    quint64 documentGeneration() const noexcept { return m_documentGeneration; }
    quint64 ruleRevision() const noexcept { return m_ruleRevision; }
    const QVector<RbtPatternRule>& rules() const noexcept { return m_rules; }
    const RbtMatchSet* matchesForRule(const QString& ruleId) const;
    int firstRuleForLine(qsizetype line) const;

private:
    QString m_filePath;
    quint64 m_documentGeneration = 0;
    quint64 m_ruleRevision = 0;
    QVector<RbtPatternRule> m_rules;
    QVector<RbtMatchSet> m_matches;
};

class VIEWER_API RbtTextDocument final
{
public:
    ~RbtTextDocument();

    RbtTextDocument(const RbtTextDocument&) = delete;
    RbtTextDocument& operator=(const RbtTextDocument&) = delete;

    static std::shared_ptr<RbtTextDocument> open(
        const QString& path, QString* error = nullptr,
        const std::shared_ptr<std::atomic_bool>& cancelled = {});

    const QString& filePath() const noexcept { return m_filePath; }
    qint64 fileSize() const noexcept { return m_fileSize; }
    qsizetype lineCount() const noexcept;
    QString lineText(qsizetype line) const;
    quint64 lineStart(qsizetype line) const;
    bool textPositionForByteOffset(quint64 byteOffset,
                                   qsizetype byteLength,
                                   qsizetype* line,
                                   qsizetype* column,
                                   qsizetype* characterLength) const;

private:
    RbtTextDocument() = default;

    QString m_filePath;
    QFile m_file;
    uchar* m_mapped = nullptr;
    qint64 m_fileSize = 0;
    std::shared_ptr<const QVector<quint64>> m_lineOffsets;
};

struct VIEWER_API RbtFindResult
{
    qint64 offset = -1;
    qsizetype byteLength = 0;
    bool wrapped = false;
    QString error;
};

class VIEWER_API RbtTextSearcher final
{
public:
    static RbtFindResult find(const QString& path,
                              QByteArray needle,
                              qint64 start,
                              bool backward,
                              bool caseSensitive,
                              const std::shared_ptr<std::atomic_bool>& cancelled = {});
};

class VIEWER_API RbtPatternRepository final
{
public:
    static bool load(const QString& path,
                     QVector<RbtPatternRule>* rules,
                     QString* error = nullptr);
    static bool save(const QString& path,
                     const QVector<RbtPatternRule>& rules,
                     QString* error = nullptr);
};

enum class RbtNavigateAction
{
    First,
    Previous,
    Next,
    Last
};

class VIEWER_API RbtTextManager final : public QObject
{
    Q_OBJECT

public:
    explicit RbtTextManager(QObject* parent = nullptr);
    ~RbtTextManager() override;

    void openFile(const QString& path);
    void clear();
    void find(const QByteArray& needle,
              qint64 start,
              bool backward,
              bool caseSensitive);

    bool applyPatterns(QVector<RbtPatternRule> rules,
                       int* errorRow = nullptr,
                       QString* error = nullptr);
    bool loadPatterns(const QString& path, QString* error = nullptr);
    bool savePatterns(const QString& path, QString* error = nullptr) const;
    static bool validatePatterns(const QVector<RbtPatternRule>& rules,
                                 int* errorRow = nullptr,
                                 QString* error = nullptr);

    const QVector<RbtPatternRule>& patterns() const noexcept { return m_patterns; }
    std::shared_ptr<const RbtTextDocument> document() const noexcept { return m_document; }
    std::shared_ptr<const RbtMatchIndex> matchIndex() const noexcept { return m_matchIndex; }
    qsizetype navigate(const QString& ruleId,
                       RbtNavigateAction action,
                       qsizetype currentLine) const;

Q_SIGNALS:
    void documentOpening(const QString& path);
    void documentReady(const QString& path);
    void documentFailed(const QString& path, const QString& reason);
    void findStarted();
    void findFinished(qint64 byteOffset, qsizetype byteLength,
                      bool wrapped, const QString& error);
    void patternScanStarted();
    void patternScanProgress(int percent);
    void patternIndexReady();
    void patternScanFailed(const QString& reason);
    void patternsChanged();

private:
    void cancelOpen();
    void cancelFind();
    void cancelPatternScan();
    void startPatternScan();

    std::shared_ptr<const RbtTextDocument> m_document;
    std::shared_ptr<const RbtMatchIndex> m_matchIndex;
    QVector<RbtPatternRule> m_patterns;
    quint64 m_documentGeneration = 0;
    quint64 m_findGeneration = 0;
    quint64 m_ruleRevision = 0;
    std::shared_ptr<std::atomic_bool> m_openCancelled;
    std::shared_ptr<std::atomic_bool> m_findCancelled;
    std::shared_ptr<std::atomic_bool> m_patternCancelled;
};

} // namespace viewer
