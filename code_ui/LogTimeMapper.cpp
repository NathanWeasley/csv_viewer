#include "LogTimeMapper.h"

#include "code_viewer/datamgr/data_manager.h"

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTime>

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
bool digit(char value) noexcept
{
    return value >= '0' && value <= '9';
}

int digits(const char* text, int count) noexcept
{
    int value = 0;
    for (int index = 0; index < count; ++index)
    {
        if (!digit(text[index]))
            return -1;
        value = value * 10 + text[index] - '0';
    }
    return value;
}

qint64 toTimestampUs(int year, int month, int day,
                     int hour, int minute, int second, int microsecond)
{
    const QDate date(year, month, day);
    const QTime time(hour, minute, second);
    if (!date.isValid() || !time.isValid() || microsecond < 0 || microsecond >= 1000000)
        return std::numeric_limits<qint64>::min();
    return QDateTime(date, time, Qt::UTC).toMSecsSinceEpoch() * 1000
        + microsecond;
}

QString formatTimestamp(qint64 timestampUs)
{
    return QDateTime::fromMSecsSinceEpoch(timestampUs / 1000, Qt::UTC)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}
}

QString LogTimeMapper::pathKey(const QString& path)
{
    QString key = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}

bool LogTimeMapper::parseRbtTimestamp(const QByteArray& line, qint64& timestampUs)
{
    const char* bytes = line.constData();
    const int size = line.size();
    for (int offset = 0; offset + 19 <= size; ++offset)
    {
        if (bytes[offset + 4] != '-' || bytes[offset + 7] != '-'
            || bytes[offset + 10] != ' ' || bytes[offset + 13] != ':'
            || bytes[offset + 16] != ':')
        {
            continue;
        }
        const int year = digits(bytes + offset, 4);
        const int month = digits(bytes + offset + 5, 2);
        const int day = digits(bytes + offset + 8, 2);
        const int hour = digits(bytes + offset + 11, 2);
        const int minute = digits(bytes + offset + 14, 2);
        const int second = digits(bytes + offset + 17, 2);
        if (year < 0 || month < 0 || day < 0 || hour < 0 || minute < 0 || second < 0)
            continue;

        int microsecond = 0;
        if (offset + 19 < size && bytes[offset + 19] == '.')
        {
            int count = 0;
            int cursor = offset + 20;
            while (cursor < size && count < 6 && digit(bytes[cursor]))
            {
                microsecond = microsecond * 10 + bytes[cursor] - '0';
                ++cursor;
                ++count;
            }
            while (count < 6)
            {
                microsecond *= 10;
                ++count;
            }
        }
        const qint64 parsed = toTimestampUs(
            year, month, day, hour, minute, second, microsecond);
        if (parsed != std::numeric_limits<qint64>::min())
        {
            timestampUs = parsed;
            return true;
        }
    }
    return false;
}

bool LogTimeMapper::parseDataDate(const std::string& value, qint64& secondTimestampUs)
{
    const char* bytes = value.data();
    const int size = static_cast<int>(value.size());
    for (int offset = 0; offset + 19 <= size; ++offset)
    {
        if (bytes[offset + 4] != '_' || bytes[offset + 7] != '_'
            || bytes[offset + 10] != '_' || bytes[offset + 13] != '_'
            || bytes[offset + 16] != '_')
        {
            continue;
        }
        const qint64 parsed = toTimestampUs(
            digits(bytes + offset, 4), digits(bytes + offset + 5, 2),
            digits(bytes + offset + 8, 2), digits(bytes + offset + 11, 2),
            digits(bytes + offset + 14, 2), digits(bytes + offset + 17, 2), 0);
        if (parsed != std::numeric_limits<qint64>::min())
        {
            secondTimestampUs = parsed;
            return true;
        }
    }
    return false;
}

bool LogTimeMapper::buildRbtIndex(const QStringList& files, QString* error)
{
    struct IndexedFile
    {
        QString path;
        QVector<Entry> entries;
        qint64 firstTimestamp = std::numeric_limits<qint64>::max();
    };

    QVector<IndexedFile> indexed;
    indexed.reserve(files.size());
    for (const QString& requestedPath : files)
    {
        const QString path = QFileInfo(requestedPath).absoluteFilePath();
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            continue;

        IndexedFile item;
        item.path = path;
        qsizetype lineNumber = 0;
        while (!file.atEnd())
        {
            const QByteArray line = file.readLine();
            qint64 timestampUs = 0;
            if (parseRbtTimestamp(line, timestampUs))
            {
                item.entries.push_back({timestampUs, -1, lineNumber});
                item.firstTimestamp = std::min(item.firstTimestamp, timestampUs);
            }
            ++lineNumber;
        }
        indexed.push_back(std::move(item));
    }

    std::stable_sort(indexed.begin(), indexed.end(),
        [](const IndexedFile& lhs, const IndexedFile& rhs)
        {
            if (lhs.firstTimestamp != rhs.firstTimestamp)
                return lhs.firstTimestamp < rhs.firstTimestamp;
            return lhs.path.compare(rhs.path, Qt::CaseInsensitive) < 0;
        });

    m_sortedFiles.clear();
    m_entries.clear();
    m_fileEntryIndices.clear();
    m_sortedFiles.reserve(indexed.size());
    m_fileEntryIndices.resize(indexed.size());
    for (int fileIndex = 0; fileIndex < indexed.size(); ++fileIndex)
    {
        m_sortedFiles.push_back(indexed[fileIndex].path);
        for (Entry entry : indexed[fileIndex].entries)
        {
            entry.fileIndex = fileIndex;
            m_entries.push_back(entry);
        }
    }
    std::stable_sort(m_entries.begin(), m_entries.end(),
        [](const Entry& lhs, const Entry& rhs)
        {
            if (lhs.timestampUs != rhs.timestampUs)
                return lhs.timestampUs < rhs.timestampUs;
            if (lhs.fileIndex != rhs.fileIndex)
                return lhs.fileIndex < rhs.fileIndex;
            return lhs.line < rhs.line;
        });
    for (int index = 0; index < m_entries.size(); ++index)
        m_fileEntryIndices[m_entries[index].fileIndex].push_back(index);
    for (auto& indices : m_fileEntryIndices)
    {
        std::sort(indices.begin(), indices.end(), [this](int lhs, int rhs)
        { return m_entries[lhs].line < m_entries[rhs].line; });
    }

    clearAlignment();
    if (m_entries.isEmpty())
    {
        if (error)
            *error = QString::fromUtf8(u8"RBT 日志中没有可识别的日期时间行。");
        return false;
    }
    if (error)
        error->clear();
    return true;
}

bool LogTimeMapper::align(const viewer::DataManager& data, QString* error)
{
    clearAlignment();
    const size_t rowCount = data.GetRowCount();
    if (rowCount == 0)
    {
        if (error)
            *error = QString::fromUtf8(u8"Viewer 当前没有可用于跳转的数据行。");
        return false;
    }
    if (data.GetDateAxisName().empty())
    {
        if (error)
        {
            *error = QString::fromUtf8(
                u8"未指定日期轴。请在“设置 → 日期轴指定”中填写日期字符串列名（例如 date_time），设置完成后重试跳转。");
        }
        return false;
    }
    const auto* dateColumn = data.GetStringColumn(data.GetDateAxisName());
    if (!dateColumn)
    {
        if (error)
        {
            *error = QString::fromUtf8(u8"指定的日期轴“%1”不是当前数据中的字符串列。")
                         .arg(QString::fromStdString(data.GetDateAxisName()));
        }
        return false;
    }
    const auto* axisColumn = data.GetColumn(data.GetXAxisColumn());
    if (!axisColumn)
    {
        if (error)
            *error = QString::fromUtf8(u8"当前数据没有可用的默认时间轴。");
        return false;
    }
    if (dateColumn->size() != rowCount || axisColumn->size() != rowCount)
    {
        if (error)
            *error = QString::fromUtf8(u8"日期轴或默认时间轴的行数与当前数据不一致。");
        return false;
    }
    if (m_entries.isEmpty())
    {
        if (error)
            *error = QString::fromUtf8(u8"RBT 日志中没有可用的时间索引。");
        return false;
    }

    struct Candidate { size_t row = 0; qint64 secondUs = 0; };
    QVector<Candidate> transitions;
    Candidate fallback;
    bool hasFallback = false;
    bool hasPrevious = false;
    qint64 previous = 0;
    qint64 firstDataTimestampUs = std::numeric_limits<qint64>::max();
    qint64 lastDataTimestampUs = std::numeric_limits<qint64>::min();
    for (size_t row = 0; row < rowCount; ++row)
    {
        qint64 secondUs = 0;
        if (!parseDataDate((*dateColumn)[row], secondUs))
            continue;
        firstDataTimestampUs = std::min(firstDataTimestampUs, secondUs);
        lastDataTimestampUs = std::max(lastDataTimestampUs, secondUs);
        if (!hasFallback)
        {
            fallback = {row, secondUs};
            hasFallback = true;
        }
        if (hasPrevious && secondUs != previous)
            transitions.push_back({row, secondUs});
        previous = secondUs;
        hasPrevious = true;
    }
    if (hasFallback)
        transitions.push_back(fallback);
    else
    {
        if (error)
        {
            *error = QString::fromUtf8(u8"日期轴“%1”中没有可识别的日期时间，期望格式包含 yyyy_MM_dd_HH_mm_ss。")
                         .arg(QString::fromStdString(data.GetDateAxisName()));
        }
        return false;
    }

    for (const Candidate& candidate : transitions)
    {
        const auto found = std::lower_bound(m_entries.cbegin(), m_entries.cend(),
            candidate.secondUs,
            [](const Entry& entry, qint64 value) { return entry.timestampUs < value; });
        if (found == m_entries.cend()
            || found->timestampUs / 1000000 != candidate.secondUs / 1000000)
        {
            continue;
        }
        const double axisValue = (*axisColumn)[candidate.row];
        if (!std::isfinite(axisValue))
            continue;
        m_baseDataIndex = candidate.row;
        m_baseRbtTimestampUs = found->timestampUs;
        m_baseAxisValue = axisValue;
        m_axisSecondsPerUnit = viewer::timeUnitToSeconds(data.GetXAxisUnit());
        if (!(m_axisSecondsPerUnit > 0.0) || !std::isfinite(m_axisSecondsPerUnit))
            m_axisSecondsPerUnit = 1.0;
        m_alignedRowCount = rowCount;
        m_aligned = true;
        if (error)
            error->clear();
        return true;
    }

    if (error)
    {
        *error = QString::fromUtf8(
            u8"未找到可用于对齐的相同秒（时间范围可能不重叠）：数据为 %1 ～ %2，RBT 为 %3 ～ %4。")
                     .arg(formatTimestamp(firstDataTimestampUs),
                          formatTimestamp(lastDataTimestampUs),
                          formatTimestamp(m_entries.front().timestampUs),
                          formatTimestamp(m_entries.back().timestampUs));
    }
    return false;
}

const LogTimeMapper::Entry* LogTimeMapper::nearestEntry(qint64 timestampUs) const
{
    if (m_entries.isEmpty())
        return nullptr;
    if (timestampUs < m_entries.front().timestampUs
        || timestampUs > m_entries.back().timestampUs)
    {
        return nullptr;
    }
    auto found = std::lower_bound(m_entries.cbegin(), m_entries.cend(), timestampUs,
        [](const Entry& entry, qint64 value) { return entry.timestampUs < value; });
    if (found == m_entries.cbegin())
        return &*found;
    if (found == m_entries.cend())
        return &m_entries.back();
    const Entry& left = *(found - 1);
    return timestampUs - left.timestampUs <= found->timestampUs - timestampUs
        ? &left : &*found;
}

const LogTimeMapper::Entry* LogTimeMapper::entryForLine(
    const QString& filePath, qsizetype line) const
{
    const QString key = pathKey(filePath);
    int fileIndex = -1;
    for (int index = 0; index < m_sortedFiles.size(); ++index)
    {
        if (pathKey(m_sortedFiles[index]) == key)
        {
            fileIndex = index;
            break;
        }
    }
    if (fileIndex < 0 || fileIndex >= m_fileEntryIndices.size()
        || m_fileEntryIndices[fileIndex].isEmpty())
    {
        return nullptr;
    }
    const auto& indices = m_fileEntryIndices[fileIndex];
    auto found = std::lower_bound(indices.cbegin(), indices.cend(), line,
        [this](int entryIndex, qsizetype value)
        { return m_entries[entryIndex].line < value; });
    if (found == indices.cbegin())
        return &m_entries[*found];
    if (found == indices.cend())
        return &m_entries[indices.back()];
    const Entry& left = m_entries[*(found - 1)];
    const Entry& right = m_entries[*found];
    return line - left.line <= right.line - line ? &left : &right;
}

bool LogTimeMapper::dataIndexForAxisValue(
    const viewer::DataManager& data, double value, size_t& dataIndex) const
{
    const viewer::Column* axis = data.GetColumn(data.GetXAxisColumn());
    if (!axis || axis->empty() || !std::isfinite(value))
        return false;
    double bestDistance = std::numeric_limits<double>::infinity();
    size_t best = 0;
    for (size_t index = 0; index < axis->size(); ++index)
    {
        const double current = (*axis)[index];
        if (!std::isfinite(current))
            continue;
        const double distance = std::abs(current - value);
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = index;
        }
    }
    if (!std::isfinite(bestDistance))
        return false;
    dataIndex = best;
    return true;
}

bool LogTimeMapper::rbtLocationForDataIndex(
    const viewer::DataManager& data, size_t dataIndex, RbtLocation& location,
    QString* error) const
{
    if (!m_aligned)
    {
        if (error)
            *error = QString::fromUtf8(u8"Viewer 数据与 RBT 日志尚未完成时间对齐。");
        return false;
    }
    if (data.GetRowCount() != m_alignedRowCount)
    {
        if (error)
            *error = QString::fromUtf8(u8"Viewer 数据行数已变化，需要重新建立时间对齐。");
        return false;
    }
    if (dataIndex >= data.GetRowCount())
    {
        if (error)
            *error = QString::fromUtf8(u8"目标数据行 %1 超出当前数据范围。").arg(dataIndex + 1);
        return false;
    }
    const viewer::Column* axis = data.GetColumn(data.GetXAxisColumn());
    if (!axis || dataIndex >= axis->size())
    {
        if (error)
            *error = QString::fromUtf8(u8"目标数据行没有对应的默认时间轴值。");
        return false;
    }
    if (!std::isfinite((*axis)[dataIndex]))
    {
        if (error)
            *error = QString::fromUtf8(u8"目标数据行的默认时间轴值不是有限数值。");
        return false;
    }
    const long double deltaUs = static_cast<long double>((*axis)[dataIndex] - m_baseAxisValue)
        * static_cast<long double>(m_axisSecondsPerUnit) * 1000000.0L;
    const qint64 targetTimestampUs =
        m_baseRbtTimestampUs + static_cast<qint64>(std::llround(deltaUs));
    const Entry* entry = nearestEntry(targetTimestampUs);
    if (!entry)
    {
        if (error)
        {
            *error = QString::fromUtf8(u8"目标时刻 %1 超出 RBT 日志范围 %2 ～ %3。")
                         .arg(formatTimestamp(targetTimestampUs),
                              formatTimestamp(m_entries.front().timestampUs),
                              formatTimestamp(m_entries.back().timestampUs));
        }
        return false;
    }
    if (entry->fileIndex < 0 || entry->fileIndex >= m_sortedFiles.size())
    {
        if (error)
            *error = QString::fromUtf8(u8"RBT 时间索引对应的文件编号无效。");
        return false;
    }
    location.filePath = m_sortedFiles[entry->fileIndex];
    location.line = entry->line;
    if (error)
        error->clear();
    return true;
}

bool LogTimeMapper::dataIndexForRbtLocation(
    const viewer::DataManager& data, const QString& filePath,
    qsizetype line, size_t& dataIndex, QString* error) const
{
    if (!m_aligned)
    {
        if (error)
            *error = QString::fromUtf8(u8"Viewer 数据与 RBT 日志尚未完成时间对齐。");
        return false;
    }
    if (data.GetRowCount() != m_alignedRowCount)
    {
        if (error)
            *error = QString::fromUtf8(u8"Viewer 数据行数已变化，需要重新建立时间对齐。");
        return false;
    }
    if (line < 0)
    {
        if (error)
            *error = QString::fromUtf8(u8"RBT 日志行号无效。");
        return false;
    }
    const Entry* entry = entryForLine(filePath, line);
    if (!entry)
    {
        if (error)
            *error = QString::fromUtf8(u8"指定的 RBT 文件或日志行不在当前时间索引中。");
        return false;
    }
    const long double deltaAxis = static_cast<long double>(
        entry->timestampUs - m_baseRbtTimestampUs)
        / (static_cast<long double>(m_axisSecondsPerUnit) * 1000000.0L);
    const double target = m_baseAxisValue + static_cast<double>(deltaAxis);
    const viewer::Column* axis = data.GetColumn(data.GetXAxisColumn());
    if (!axis || axis->empty())
    {
        if (error)
            *error = QString::fromUtf8(u8"当前数据没有可用的默认时间轴。");
        return false;
    }
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = -std::numeric_limits<double>::infinity();
    for (size_t index = 0; index < axis->size(); ++index)
    {
        if (std::isfinite((*axis)[index]))
        {
            minimum = std::min(minimum, (*axis)[index]);
            maximum = std::max(maximum, (*axis)[index]);
        }
    }
    if (!std::isfinite(minimum) || !std::isfinite(maximum))
    {
        if (error)
            *error = QString::fromUtf8(u8"默认时间轴中没有有限数值。");
        return false;
    }
    if (target < minimum || target > maximum)
    {
        if (error)
        {
            *error = QString::fromUtf8(u8"RBT 目标时刻换算后的轴值 %1 超出 Viewer 范围 %2 ～ %3。")
                         .arg(target, 0, 'g', 12)
                         .arg(minimum, 0, 'g', 12)
                         .arg(maximum, 0, 'g', 12);
        }
        return false;
    }
    if (!dataIndexForAxisValue(data, target, dataIndex))
    {
        if (error)
            *error = QString::fromUtf8(u8"无法在默认时间轴中找到与 RBT 时刻对应的数据行。");
        return false;
    }
    if (error)
        error->clear();
    return true;
}
