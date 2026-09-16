#include "code_viewer/textmgr/rbt_text_manager.h"

#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMetaObject>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <exception>
#include <functional>
#include <limits>
#include <new>

namespace viewer
{
namespace
{

constexpr qint64 kIndexChunkSize = 8LL * 1024LL * 1024LL;
constexpr qsizetype kMaximumIndexedLines = 25'000'000;
constexpr qint64 kAutomaticPatternScanMaximumBytes = 128LL * 1024LL * 1024LL;
constexpr qsizetype kMaximumPatternMatches = 10'000'000;
constexpr int kPatternFileVersion = 1;

bool isCancelled(const std::shared_ptr<std::atomic_bool>& cancelled)
{
    return cancelled && cancelled->load(std::memory_order_relaxed);
}

QString taskFailure(const char* operation, const char* detail = nullptr)
{
    QString message = QStringLiteral("RBT background task failed: ")
        + QString::fromLatin1(operation);
    if (detail && *detail)
        message += QStringLiteral(" (") + QString::fromUtf8(detail) + QLatin1Char(')');
    return message;
}

QString taskOutOfMemory(const char* operation)
{
    return QStringLiteral("Insufficient memory while processing RBT log: ")
        + QString::fromLatin1(operation);
}

QString matchTypeKey(RbtPatternMatchType type)
{
    switch (type)
    {
    case RbtPatternMatchType::Exact: return QStringLiteral("exact");
    case RbtPatternMatchType::RegularExpression: return QStringLiteral("regex");
    case RbtPatternMatchType::Fuzzy: return QStringLiteral("fuzzy");
    }
    return QStringLiteral("regex");
}

RbtPatternMatchType matchTypeFromKey(const QString& key)
{
    if (key.compare(QStringLiteral("exact"), Qt::CaseInsensitive) == 0)
        return RbtPatternMatchType::Exact;
    if (key.compare(QStringLiteral("fuzzy"), Qt::CaseInsensitive) == 0)
        return RbtPatternMatchType::Fuzzy;
    // Missing matchType is the legacy version-1 format and must retain the
    // previous direct regular-expression behavior.
    return RbtPatternMatchType::RegularExpression;
}

QString fuzzyRegularExpression(const QString& pattern)
{
    QString result;
    QString literal;
    bool gapPending = false;
    const auto flushLiteral = [&]()
    {
        if (literal.isEmpty())
            return;
        result += QRegularExpression::escape(literal);
        literal.clear();
    };

    for (const QChar character : pattern)
    {
        if (character.isSpace())
        {
            flushLiteral();
            gapPending = true;
            continue;
        }
        if (gapPending)
        {
            // Explicitly exclude line breaks even if this expression is ever
            // reused against multi-line text in the future.
            result += QStringLiteral("[^\\r\\n]*");
            gapPending = false;
        }
        literal += character;
    }
    flushLiteral();
    if (gapPending)
        result += QStringLiteral("[^\\r\\n]*");
    return result;
}

QByteArray comparableBytes(QByteArray bytes, bool caseSensitive)
{
    return caseSensitive ? bytes : bytes.toLower();
}

qint64 findForwardRange(QFile& file,
                        const QByteArray& needle,
                        qint64 begin,
                        qint64 end,
                        bool caseSensitive,
                        const std::shared_ptr<std::atomic_bool>& cancelled)
{
    if (begin >= end)
        return -1;
    QByteArray tail;
    qint64 position = begin;
    while (position < end)
    {
        if (isCancelled(cancelled) || !file.seek(position))
            return -1;
        const QByteArray current = file.read(std::min(kIndexChunkSize, end - position));
        if (current.isEmpty())
            break;
        const QByteArray combined = tail + current;
        const qint64 base = position - tail.size();
        const QByteArray comparable = comparableBytes(combined, caseSensitive);
        qsizetype found = comparable.indexOf(needle);
        while (found >= 0)
        {
            const qint64 absolute = base + found;
            if (absolute >= begin && absolute + needle.size() <= end)
                return absolute;
            found = comparable.indexOf(needle, found + 1);
        }
        const qsizetype overlap = std::min<qsizetype>(
            std::max<qsizetype>(0, needle.size() - 1), combined.size());
        tail = combined.right(overlap);
        position += current.size();
    }
    return -1;
}

qint64 findLastInRange(QFile& file,
                       const QByteArray& needle,
                       qint64 begin,
                       qint64 end,
                       bool caseSensitive,
                       const std::shared_ptr<std::atomic_bool>& cancelled)
{
    if (begin >= end)
        return -1;
    QByteArray tail;
    qint64 position = begin;
    qint64 last = -1;
    while (position < end)
    {
        if (isCancelled(cancelled) || !file.seek(position))
            return -1;
        const QByteArray current = file.read(std::min(kIndexChunkSize, end - position));
        if (current.isEmpty())
            break;
        const QByteArray combined = tail + current;
        const qint64 base = position - tail.size();
        const QByteArray comparable = comparableBytes(combined, caseSensitive);
        qsizetype found = comparable.indexOf(needle);
        while (found >= 0)
        {
            const qint64 absolute = base + found;
            if (absolute >= begin && absolute + needle.size() <= end)
                last = absolute;
            found = comparable.indexOf(needle, found + 1);
        }
        const qsizetype overlap = std::min<qsizetype>(
            std::max<qsizetype>(0, needle.size() - 1), combined.size());
        tail = combined.right(overlap);
        position += current.size();
    }
    return last;
}

struct OpenResult
{
    std::shared_ptr<RbtTextDocument> document;
    QString error;
};

struct PatternScanResult
{
    std::shared_ptr<RbtMatchIndex> index;
    QString error;
    bool cancelled = false;
};

} // namespace

RbtMatchSet::RbtMatchSet(QVector<quint32> sortedLines, qsizetype totalLineCount)
    : m_count(sortedLines.size())
{
    // 稠密命中改用位图，避免“匹配所有行”时每条规则消耗数百 MB。
    if (totalLineCount > 0 && sortedLines.size() > totalLineCount / 32
        && totalLineCount <= std::numeric_limits<int>::max())
    {
        m_denseLines.resize(static_cast<int>(totalLineCount));
        for (const quint32 line : sortedLines)
            m_denseLines.setBit(static_cast<int>(line));
    }
    else
    {
        m_sparseLines = std::move(sortedLines);
    }
}

bool RbtMatchSet::contains(qsizetype line) const
{
    if (line < 0)
        return false;
    if (!m_denseLines.isEmpty())
        return line < m_denseLines.size() && m_denseLines.testBit(static_cast<int>(line));
    return std::binary_search(m_sparseLines.cbegin(), m_sparseLines.cend(),
                              static_cast<quint32>(line));
}

qsizetype RbtMatchSet::first() const
{
    if (m_count == 0)
        return -1;
    if (m_denseLines.isEmpty())
        return static_cast<qsizetype>(m_sparseLines.front());
    for (int line = 0; line < m_denseLines.size(); ++line)
    {
        if (m_denseLines.testBit(line))
            return line;
    }
    return -1;
}

qsizetype RbtMatchSet::last() const
{
    if (m_count == 0)
        return -1;
    if (m_denseLines.isEmpty())
        return static_cast<qsizetype>(m_sparseLines.back());
    for (int line = m_denseLines.size() - 1; line >= 0; --line)
    {
        if (m_denseLines.testBit(line))
            return line;
    }
    return -1;
}

qsizetype RbtMatchSet::next(qsizetype line) const
{
    if (m_count == 0)
        return -1;
    if (line < 0)
        return first();
    if (m_denseLines.isEmpty())
    {
        const auto found = std::upper_bound(
            m_sparseLines.cbegin(), m_sparseLines.cend(), static_cast<quint32>(line));
        return found == m_sparseLines.cend() ? -1 : static_cast<qsizetype>(*found);
    }
    for (qsizetype candidate = std::max<qsizetype>(0, line + 1);
         candidate < m_denseLines.size(); ++candidate)
    {
        if (m_denseLines.testBit(static_cast<int>(candidate)))
            return candidate;
    }
    return -1;
}

qsizetype RbtMatchSet::previous(qsizetype line) const
{
    if (m_count == 0 || line <= 0)
        return -1;
    if (m_denseLines.isEmpty())
    {
        const auto found = std::lower_bound(
            m_sparseLines.cbegin(), m_sparseLines.cend(), static_cast<quint32>(line));
        return found == m_sparseLines.cbegin() ? -1 : static_cast<qsizetype>(*(found - 1));
    }
    for (qsizetype candidate = std::min<qsizetype>(line - 1, m_denseLines.size() - 1);
         candidate >= 0; --candidate)
    {
        if (m_denseLines.testBit(static_cast<int>(candidate)))
            return candidate;
    }
    return -1;
}

RbtMatchIndex::RbtMatchIndex(QString filePath,
                             quint64 documentGeneration,
                             quint64 ruleRevision,
                             QVector<RbtPatternRule> rules,
                             QVector<RbtMatchSet> matches)
    : m_filePath(std::move(filePath))
    , m_documentGeneration(documentGeneration)
    , m_ruleRevision(ruleRevision)
    , m_rules(std::move(rules))
    , m_matches(std::move(matches))
{
}

const RbtMatchSet* RbtMatchIndex::matchesForRule(const QString& ruleId) const
{
    for (qsizetype index = 0; index < m_rules.size() && index < m_matches.size(); ++index)
    {
        if (m_rules[index].id == ruleId)
            return &m_matches[index];
    }
    return nullptr;
}

int RbtMatchIndex::firstRuleForLine(qsizetype line) const
{
    for (qsizetype index = 0; index < m_matches.size(); ++index)
    {
        if (m_matches[index].contains(line))
            return static_cast<int>(index);
    }
    return -1;
}

RbtTextDocument::~RbtTextDocument()
{
    if (m_mapped)
        m_file.unmap(m_mapped);
    m_mapped = nullptr;
    m_file.close();
}

std::shared_ptr<RbtTextDocument> RbtTextDocument::open(
    const QString& path, QString* error,
    const std::shared_ptr<std::atomic_bool>& cancelled)
{
    QFile indexFile(path);
    if (!indexFile.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = indexFile.errorString();
        return {};
    }

    const qint64 fileSize = indexFile.size();
    if (fileSize < 0
        || static_cast<quint64>(fileSize) > std::numeric_limits<quint32>::max())
    {
        if (error)
            *error = QStringLiteral("RBT viewer supports files up to 4 GiB.");
        return {};
    }
    auto offsets = std::make_shared<QVector<quint32>>();
    offsets->reserve(static_cast<qsizetype>(
        std::min<qint64>(fileSize / 48 + 1, 4'000'000)));
    offsets->push_back(0);
    quint64 absoluteOffset = 0;
    while (!indexFile.atEnd())
    {
        if (isCancelled(cancelled))
            return {};
        const QByteArray chunk = indexFile.read(kIndexChunkSize);
        if (chunk.isEmpty() && indexFile.error() != QFile::NoError)
        {
            if (error)
                *error = indexFile.errorString();
            return {};
        }
        const char* data = chunk.constData();
        for (qsizetype index = 0; index < chunk.size(); ++index)
        {
            if (data[index] != '\n')
                continue;
            if (offsets->size() >= kMaximumIndexedLines)
            {
                if (error)
                    *error = QString::fromUtf8(u8"日志行数超过查看器上限（2500 万行）。");
                return {};
            }
            offsets->push_back(static_cast<quint32>(
                absoluteOffset + static_cast<quint64>(index) + 1));
        }
        absoluteOffset += static_cast<quint64>(chunk.size());
    }

    auto document = std::shared_ptr<RbtTextDocument>(new RbtTextDocument());
    document->m_filePath = QFileInfo(path).absoluteFilePath();
    document->m_file.setFileName(document->m_filePath);
    if (!document->m_file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = document->m_file.errorString();
        return {};
    }
    document->m_fileSize = document->m_file.size();
    if (document->m_fileSize > 0)
    {
        document->m_mapped = document->m_file.map(0, document->m_fileSize);
        if (!document->m_mapped)
        {
            if (error)
                *error = document->m_file.errorString();
            return {};
        }
    }
    document->m_lineOffsets = std::move(offsets);
    if (error)
        error->clear();
    return document;
}

qsizetype RbtTextDocument::lineCount() const noexcept
{
    return m_lineOffsets ? m_lineOffsets->size() : 0;
}

QString RbtTextDocument::lineText(qsizetype line) const
{
    if (!m_lineOffsets || line < 0 || line >= m_lineOffsets->size() || !m_mapped)
        return {};
    const quint64 begin = (*m_lineOffsets)[line];
    quint64 end = line + 1 < m_lineOffsets->size()
        ? (*m_lineOffsets)[line + 1] : static_cast<quint64>(m_fileSize);
    if (end > begin && m_mapped[end - 1] == '\n')
        --end;
    if (end > begin && m_mapped[end - 1] == '\r')
        --end;
    return QString::fromUtf8(
        reinterpret_cast<const char*>(m_mapped + begin),
        static_cast<qsizetype>(end - begin));
}

quint64 RbtTextDocument::lineStart(qsizetype line) const
{
    if (!m_lineOffsets || m_lineOffsets->isEmpty())
        return 0;
    return static_cast<quint64>(
        (*m_lineOffsets)[std::clamp<qsizetype>(line, 0, m_lineOffsets->size() - 1)]);
}

bool RbtTextDocument::textPositionForByteOffset(
    quint64 byteOffset, qsizetype byteLength, qsizetype* line,
    qsizetype* column, qsizetype* characterLength) const
{
    if (!m_lineOffsets || m_lineOffsets->isEmpty() || !m_mapped
        || byteOffset > static_cast<quint64>(m_fileSize))
        return false;
    const auto found = std::upper_bound(
        m_lineOffsets->cbegin(), m_lineOffsets->cend(), byteOffset);
    const qsizetype foundLine = std::max<qsizetype>(0,
        static_cast<qsizetype>(found - m_lineOffsets->cbegin()) - 1);
    const quint64 start = lineStart(foundLine);
    if (line)
        *line = foundLine;
    if (column)
    {
        *column = QString::fromUtf8(
            reinterpret_cast<const char*>(m_mapped + start),
            static_cast<qsizetype>(byteOffset - start)).size();
    }
    if (characterLength)
    {
        *characterLength = QString::fromUtf8(
            reinterpret_cast<const char*>(m_mapped + byteOffset), byteLength).size();
    }
    return true;
}

RbtFindResult RbtTextSearcher::find(
    const QString& path, QByteArray needle, qint64 start, bool backward,
    bool caseSensitive, const std::shared_ptr<std::atomic_bool>& cancelled)
{
    RbtFindResult result;
    result.byteLength = needle.size();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.error = file.errorString();
        return result;
    }
    if (needle.isEmpty() || isCancelled(cancelled))
        return result;
    if (!caseSensitive)
        needle = needle.toLower();
    const qint64 size = file.size();
    start = std::clamp<qint64>(start, 0, size);
    if (backward)
    {
        result.offset = findLastInRange(file, needle, 0, start, caseSensitive, cancelled);
        if (result.offset < 0 && !isCancelled(cancelled))
        {
            result.offset = findLastInRange(file, needle, start, size, caseSensitive, cancelled);
            result.wrapped = result.offset >= 0;
        }
    }
    else
    {
        result.offset = findForwardRange(file, needle, start, size, caseSensitive, cancelled);
        if (result.offset < 0 && !isCancelled(cancelled))
        {
            result.offset = findForwardRange(file, needle, 0, start, caseSensitive, cancelled);
            result.wrapped = result.offset >= 0;
        }
    }
    return result;
}

bool RbtPatternRepository::load(
    const QString& path, QVector<RbtPatternRule>* rules, QString* error)
{
    if (!rules)
    {
        if (error)
            *error = QString::fromUtf8(u8"缺少规则输出容器。");
        return false;
    }
    rules->clear();
    QFile file(path);
    if (!file.exists())
        return true;
    if (!file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (error)
            *error = parseError.error != QJsonParseError::NoError
                ? parseError.errorString() : QString::fromUtf8(u8"规则文件根节点必须是对象。");
        return false;
    }
    const QJsonArray array = document.object().value(QStringLiteral("patterns")).toArray();
    for (const QJsonValue& value : array)
    {
        if (!value.isObject())
            continue;
        const QJsonObject object = value.toObject();
        RbtPatternRule rule;
        rule.id = object.value(QStringLiteral("id")).toString();
        if (rule.id.isEmpty())
            rule.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        rule.name = object.value(QStringLiteral("name")).toString();
        rule.expression = object.value(QStringLiteral("expression")).toString();
        rule.matchType = matchTypeFromKey(
            object.value(QStringLiteral("matchType")).toString());
        rule.color = QColor(object.value(QStringLiteral("color")).toString());
        if (!rule.color.isValid())
            rule.color = QColor(255, 235, 59, 80);
        rules->push_back(std::move(rule));
    }
    if (error)
        error->clear();
    return true;
}

bool RbtPatternRepository::save(
    const QString& path, const QVector<RbtPatternRule>& rules, QString* error)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        if (error)
            *error = QString::fromUtf8(u8"无法创建规则目录。");
        return false;
    }
    QJsonArray array;
    for (const RbtPatternRule& rule : rules)
    {
        QJsonObject object;
        object[QStringLiteral("id")] = rule.id;
        object[QStringLiteral("name")] = rule.name;
        object[QStringLiteral("expression")] = rule.expression;
        object[QStringLiteral("matchType")] = matchTypeKey(rule.matchType);
        object[QStringLiteral("color")] = rule.color.name(QColor::HexArgb);
        array.push_back(object);
    }
    QJsonObject root;
    root[QStringLiteral("version")] = kPatternFileVersion;
    root[QStringLiteral("patterns")] = array;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0
        || !file.commit())
    {
        if (error)
            *error = file.errorString();
        return false;
    }
    if (error)
        error->clear();
    return true;
}

RbtTextManager::RbtTextManager(QObject* parent)
    : QObject(parent)
{
}

RbtTextManager::~RbtTextManager()
{
    cancelOpen();
    cancelFind();
    cancelPatternScan();
}

void RbtTextManager::cancelOpen()
{
    if (m_openCancelled)
        m_openCancelled->store(true, std::memory_order_relaxed);
}

void RbtTextManager::cancelFind()
{
    if (m_findCancelled)
        m_findCancelled->store(true, std::memory_order_relaxed);
}

void RbtTextManager::cancelPatternScan()
{
    if (m_patternCancelled)
        m_patternCancelled->store(true, std::memory_order_relaxed);
}

void RbtTextManager::openFile(const QString& path)
{
    cancelOpen();
    cancelFind();
    cancelPatternScan();
    const quint64 generation = ++m_documentGeneration;
    ++m_findGeneration;
    m_document.reset();
    m_matchIndex.reset();
    m_openCancelled = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_openCancelled;
    emit documentOpening(path);

    auto* watcher = new QFutureWatcher<OpenResult>(this);
    connect(watcher, &QFutureWatcher<OpenResult>::finished, this,
        [this, watcher, generation, path]()
        {
            OpenResult result;
            try
            {
                result = watcher->future().takeResult();
            }
            catch (const std::bad_alloc&)
            {
                result.error = taskOutOfMemory("opening document");
            }
            catch (const std::exception& exception)
            {
                result.error = taskFailure("opening document", exception.what());
            }
            catch (...)
            {
                result.error = taskFailure("opening document");
            }
            watcher->deleteLater();
            if (generation != m_documentGeneration || isCancelled(m_openCancelled))
                return;
            if (!result.document)
            {
                emit documentFailed(path, result.error.isEmpty()
                    ? QString::fromUtf8(u8"日志打开任务已取消。") : result.error);
                return;
            }
            m_document = result.document;
            emit documentReady(m_document->filePath());
            startPatternScan();
        });
    watcher->setFuture(QtConcurrent::run([path, cancelled]()
    {
        OpenResult result;
        try
        {
            result.document = RbtTextDocument::open(path, &result.error, cancelled);
        }
        catch (const std::bad_alloc&)
        {
            result.error = taskOutOfMemory("building line index");
        }
        catch (const std::exception& exception)
        {
            result.error = taskFailure("building line index", exception.what());
        }
        catch (...)
        {
            result.error = taskFailure("building line index");
        }
        return result;
    }));
}

void RbtTextManager::clear()
{
    cancelOpen();
    cancelFind();
    cancelPatternScan();
    ++m_documentGeneration;
    ++m_findGeneration;
    m_document.reset();
    m_matchIndex.reset();
}

void RbtTextManager::find(
    const QByteArray& needle, qint64 start, bool backward, bool caseSensitive)
{
    if (!m_document || needle.isEmpty())
        return;
    cancelFind();
    const quint64 generation = ++m_findGeneration;
    m_findCancelled = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = m_findCancelled;
    const QString path = m_document->filePath();
    emit findStarted();
    auto* watcher = new QFutureWatcher<RbtFindResult>(this);
    connect(watcher, &QFutureWatcher<RbtFindResult>::finished, this,
        [this, watcher, generation]()
        {
            RbtFindResult result;
            try
            {
                result = watcher->future().takeResult();
            }
            catch (const std::bad_alloc&)
            {
                result.error = taskOutOfMemory("searching document");
            }
            catch (const std::exception& exception)
            {
                result.error = taskFailure("searching document", exception.what());
            }
            catch (...)
            {
                result.error = taskFailure("searching document");
            }
            watcher->deleteLater();
            if (generation != m_findGeneration || isCancelled(m_findCancelled))
                return;
            emit findFinished(result.offset, result.byteLength,
                              result.wrapped, result.error);
        });
    watcher->setFuture(QtConcurrent::run(
        [path, needle, start, backward, caseSensitive, cancelled]()
        {
            RbtFindResult result;
            try
            {
                result = RbtTextSearcher::find(
                    path, needle, start, backward, caseSensitive, cancelled);
            }
            catch (const std::bad_alloc&)
            {
                result.error = taskOutOfMemory("searching document");
            }
            catch (const std::exception& exception)
            {
                result.error = taskFailure("searching document", exception.what());
            }
            catch (...)
            {
                result.error = taskFailure("searching document");
            }
            return result;
        }));
}

bool RbtTextManager::validatePatterns(
    const QVector<RbtPatternRule>& rules, int* errorRow, QString* error)
{
    for (qsizetype row = 0; row < rules.size(); ++row)
    {
        const RbtPatternRule& rule = rules[row];
        if (rule.name.trimmed().isEmpty())
        {
            if (errorRow) *errorRow = static_cast<int>(row);
            if (error) *error = QString::fromUtf8(u8"模式名称不能为空。");
            return false;
        }
        if (rule.expression.isEmpty())
        {
            if (errorRow) *errorRow = static_cast<int>(row);
            if (error) *error = QString::fromUtf8(u8"匹配模式不能为空。");
            return false;
        }
        for (qsizetype previous = 0; previous < row; ++previous)
        {
            if (rules[previous].name.trimmed().compare(
                    rule.name.trimmed(), Qt::CaseInsensitive) == 0)
            {
                if (errorRow) *errorRow = static_cast<int>(row);
                if (error) *error = QString::fromUtf8(u8"模式名称不能重复。");
                return false;
            }
        }
        const QRegularExpression expression = compilePattern(rule);
        if (!expression.isValid())
        {
            if (errorRow) *errorRow = static_cast<int>(row);
            if (error)
            {
                *error = QString::fromUtf8(u8"匹配模式转换后的正则表达式错误（位置 %1）：%2")
                    .arg(expression.patternErrorOffset())
                    .arg(expression.errorString());
            }
            return false;
        }
        if (!rule.color.isValid())
        {
            if (errorRow) *errorRow = static_cast<int>(row);
            if (error) *error = QString::fromUtf8(u8"高亮颜色无效。");
            return false;
        }
    }
    if (errorRow) *errorRow = -1;
    if (error) error->clear();
    return true;
}

QRegularExpression RbtTextManager::compilePattern(const RbtPatternRule& rule)
{
    QRegularExpression::PatternOptions options =
        QRegularExpression::UseUnicodePropertiesOption;
    QString expression;
    switch (rule.matchType)
    {
    case RbtPatternMatchType::Exact:
        expression = QStringLiteral("\\A(?:%1)\\z")
            .arg(QRegularExpression::escape(rule.expression));
        break;
    case RbtPatternMatchType::RegularExpression:
        expression = rule.expression;
        break;
    case RbtPatternMatchType::Fuzzy:
        expression = fuzzyRegularExpression(rule.expression);
        options |= QRegularExpression::CaseInsensitiveOption;
        break;
    }
    return QRegularExpression(expression, options);
}

bool RbtTextManager::applyPatterns(
    QVector<RbtPatternRule> rules, int* errorRow, QString* error)
{
    for (RbtPatternRule& rule : rules)
    {
        rule.name = rule.name.trimmed();
        if (rule.id.isEmpty())
            rule.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (rule.color.alpha() == 255)
            rule.color.setAlpha(80);
    }
    if (!validatePatterns(rules, errorRow, error))
        return false;
    m_patterns = std::move(rules);
    ++m_ruleRevision;
    cancelPatternScan();
    m_matchIndex.reset();
    emit patternsChanged();
    // This is an explicit user action, so allow scanning a large document.
    startPatternScan(true);
    return true;
}

bool RbtTextManager::loadPatterns(const QString& path, QString* error)
{
    QVector<RbtPatternRule> rules;
    if (!RbtPatternRepository::load(path, &rules, error))
        return false;
    int errorRow = -1;
    QString validationError;
    if (!validatePatterns(rules, &errorRow, &validationError))
    {
        if (error)
            *error = QString::fromUtf8(u8"规则文件第 %1 行无效：%2")
                .arg(errorRow + 1).arg(validationError);
        return false;
    }
    m_patterns = std::move(rules);
    ++m_ruleRevision;
    emit patternsChanged();
    startPatternScan();
    return true;
}

bool RbtTextManager::savePatterns(const QString& path, QString* error) const
{
    return RbtPatternRepository::save(path, m_patterns, error);
}

void RbtTextManager::startPatternScan(bool force)
{
    cancelPatternScan();
    m_patternCancelled = std::make_shared<std::atomic_bool>(false);
    if (!m_document)
        return;

    const auto document = m_document;
    const QVector<RbtPatternRule> patterns = m_patterns;
    const quint64 documentGeneration = m_documentGeneration;
    const quint64 ruleRevision = m_ruleRevision;
    const auto cancelled = m_patternCancelled;
    if (patterns.isEmpty())
    {
        m_matchIndex = std::make_shared<RbtMatchIndex>(
            document->filePath(), documentGeneration, ruleRevision,
            patterns, QVector<RbtMatchSet>{});
        emit patternIndexReady();
        return;
    }
    if (!force && document->fileSize() > kAutomaticPatternScanMaximumBytes)
    {
        emit patternScanDeferred(
            QStringLiteral("Automatic pattern scan was deferred for this large RBT log. "
                           "Use Update on the pattern tab to run it explicitly."));
        return;
    }

    emit patternScanStarted();
    QPointer<RbtTextManager> self(this);
    auto* watcher = new QFutureWatcher<PatternScanResult>(this);
    connect(watcher, &QFutureWatcher<PatternScanResult>::finished, this,
        [this, watcher, documentGeneration, ruleRevision]()
        {
            PatternScanResult result;
            try
            {
                result = watcher->future().takeResult();
            }
            catch (const std::bad_alloc&)
            {
                result.error = taskOutOfMemory("building pattern index");
            }
            catch (const std::exception& exception)
            {
                result.error = taskFailure("building pattern index", exception.what());
            }
            catch (...)
            {
                result.error = taskFailure("building pattern index");
            }
            watcher->deleteLater();
            if (documentGeneration != m_documentGeneration
                || ruleRevision != m_ruleRevision
                || isCancelled(m_patternCancelled))
                return;
            if (!result.error.isEmpty())
            {
                emit patternScanFailed(result.error);
                return;
            }
            if (!result.index || result.cancelled)
                return;
            m_matchIndex = result.index;
            emit patternScanProgress(100);
            emit patternIndexReady();
        });
    watcher->setFuture(QtConcurrent::run(
        [document, patterns, documentGeneration, ruleRevision, cancelled, self]()
        {
            PatternScanResult result;
            try
            {
                QVector<QRegularExpression> expressions;
                expressions.reserve(patterns.size());
                for (const RbtPatternRule& rule : patterns)
                    expressions.push_back(RbtTextManager::compilePattern(rule));

                QVector<QVector<quint32>> matchedLines(patterns.size());
                qsizetype totalMatches = 0;
                const qsizetype lineCount = document->lineCount();
                for (qsizetype line = 0; line < lineCount; ++line)
                {
                    if (isCancelled(cancelled))
                    {
                        result.cancelled = true;
                        return result;
                    }
                    const QString text = document->lineText(line);
                    if (!text.isEmpty())
                    {
                        for (qsizetype pattern = 0; pattern < expressions.size(); ++pattern)
                        {
                            if (!expressions[pattern].match(text).hasMatch())
                                continue;
                            if (totalMatches >= kMaximumPatternMatches)
                            {
                                result.error = QStringLiteral(
                                    "RBT pattern index exceeds the 10 million match safety limit.");
                                return result;
                            }
                            matchedLines[pattern].push_back(static_cast<quint32>(line));
                            ++totalMatches;
                        }
                    }
                    if (self && lineCount > 0 && (line & 0x3fff) == 0)
                    {
                        const int percent = static_cast<int>(line * 100 / lineCount);
                        QMetaObject::invokeMethod(self,
                            [self, documentGeneration, ruleRevision, percent]()
                            {
                                if (self && self->m_documentGeneration == documentGeneration
                                    && self->m_ruleRevision == ruleRevision)
                                    emit self->patternScanProgress(percent);
                            }, Qt::QueuedConnection);
                    }
                }

                QVector<RbtMatchSet> matchSets;
                matchSets.reserve(matchedLines.size());
                for (QVector<quint32>& lines : matchedLines)
                    matchSets.push_back(RbtMatchSet(std::move(lines), lineCount));
                result.index = std::make_shared<RbtMatchIndex>(
                    document->filePath(), documentGeneration, ruleRevision,
                    patterns, std::move(matchSets));
            }
            catch (const std::bad_alloc&)
            {
                result.error = taskOutOfMemory("building pattern index");
            }
            catch (const std::exception& exception)
            {
                result.error = taskFailure("building pattern index", exception.what());
            }
            catch (...)
            {
                result.error = taskFailure("building pattern index");
            }
            return result;
        }));
}

qsizetype RbtTextManager::navigate(
    const QString& ruleId, RbtNavigateAction action, qsizetype currentLine) const
{
    if (!m_matchIndex)
        return -1;
    const RbtMatchSet* matches = m_matchIndex->matchesForRule(ruleId);
    if (!matches)
        return -1;
    switch (action)
    {
    case RbtNavigateAction::First: return matches->first();
    case RbtNavigateAction::Previous: return matches->previous(currentLine);
    case RbtNavigateAction::Next: return matches->next(currentLine);
    case RbtNavigateAction::Last: return matches->last();
    }
    return -1;
}

} // namespace viewer
