#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstddef>
#include <cstdint>
#include <string>

namespace viewer { class DataManager; }

// Builds a compact timestamp/line index for parsed RBT text files and aligns it
// with a configured HikLog/CSV date column plus DataManager's default time axis.
class LogTimeMapper
{
public:
    struct RbtLocation
    {
        QString filePath;
        qsizetype line = -1; // zero-based
    };

    bool buildRbtIndex(const QStringList& files, QString* error = nullptr);
    bool align(const viewer::DataManager& data, QString* error = nullptr);
    void clearAlignment() noexcept { m_aligned = false; }

    bool isIndexed() const noexcept { return !m_entries.isEmpty(); }
    bool isAligned() const noexcept { return m_aligned; }
    const QStringList& rbtFiles() const noexcept { return m_sortedFiles; }

    bool rbtLocationForDataIndex(const viewer::DataManager& data,
                                 size_t dataIndex,
                                 RbtLocation& location) const;
    bool dataIndexForRbtLocation(const viewer::DataManager& data,
                                 const QString& filePath,
                                 qsizetype line,
                                 size_t& dataIndex) const;
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
