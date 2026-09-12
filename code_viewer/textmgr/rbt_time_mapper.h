#pragma once

#include "code_viewer/base/base_def.h"

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstddef>
#include <cstdint>
#include <string>

namespace viewer
{

class DataManager;

// 为已解析的 RBT 文本建立紧凑的时间戳/行号索引，并将其与
// HikLog/CSV 的日期列及 DataManager 默认时间轴对齐。
class VIEWER_API RbtTimeMapper
{
public:
    struct RbtLocation
    {
        QString filePath;
        qsizetype line = -1; // 从零开始计数
    };

    bool buildRbtIndex(const QStringList& files, QString* error = nullptr);
    bool align(const viewer::DataManager& data, QString* error = nullptr);
    void clearAlignment() noexcept { m_aligned = false; }

    bool isIndexed() const noexcept { return !m_entries.isEmpty(); }
    bool isAligned() const noexcept { return m_aligned; }
    const QStringList& rbtFiles() const noexcept { return m_sortedFiles; }

    bool rbtLocationForDataIndex(const viewer::DataManager& data,
                                 size_t dataIndex,
                                 RbtLocation& location,
                                 QString* error = nullptr) const;
    bool dataIndexForRbtLocation(const viewer::DataManager& data,
                                 const QString& filePath,
                                 qsizetype line,
                                 size_t& dataIndex,
                                 QString* error = nullptr) const;
    bool dataIndexForAxisValue(const viewer::DataManager& data,
                               double value,
                               size_t& dataIndex) const;

private:
    struct Entry
    {
        qint64 timestampUs = 0;
        int fileIndex = -1;
        qsizetype line = -1;
    };

    static bool parseRbtTimestamp(const QByteArray& line, qint64& timestampUs);
    static bool parseDataDate(const std::string& value, qint64& secondTimestampUs);
    static QString pathKey(const QString& path);
    const Entry* entryForLine(const QString& filePath, qsizetype line) const;
    const Entry* nearestEntry(qint64 timestampUs) const;

    QStringList m_sortedFiles;
    QVector<Entry> m_entries;
    QVector<QVector<int>> m_fileEntryIndices;

    bool m_aligned = false;
    size_t m_baseDataIndex = 0;
    qint64 m_baseRbtTimestampUs = 0;
    double m_baseAxisValue = 0.0;
    double m_axisSecondsPerUnit = 1.0;
    size_t m_alignedRowCount = 0;
};

} // namespace viewer
