#pragma once

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLockFile>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QThread>

#include <array>

namespace viewer
{
namespace trace
{

enum class Category
{
    Operation,
    FileIO,
    Bookmark,
    Layout,
    XAxis,
    Expression,
    Shutdown,
    PlotOpenGL
};

struct CategoryInfo
{
    Category category;
    const char* settingsKey;
    const char* displayNameUtf8;
};

inline const std::array<CategoryInfo, 8>& categories()
{
    static const std::array<CategoryInfo, 8> result{{
        { Category::Operation,  "operation",  u8"常规操作" },
        { Category::FileIO,     "fileIO",     u8"文件读取与导出" },
        { Category::Bookmark,   "bookmark",   u8"书签" },
        { Category::Layout,     "layout",     u8"视图与布局" },
        { Category::XAxis,      "xAxis",      u8"X轴与联动" },
        { Category::Expression, "expression", u8"表达式" },
        { Category::Shutdown,   "shutdown",   u8"关闭与退出" },
        { Category::PlotOpenGL, "plotOpenGL", u8"OpenGL与绘图环境" }
    }};
    return result;
}

inline const CategoryInfo& categoryInfo(Category category)
{
    for (const auto& info : categories())
    {
        if (info.category == category)
            return info;
    }
    return categories().front();
}

inline QString userDirectory()
{
    const QString path = QCoreApplication::applicationDirPath() + "/user";
    QDir().mkpath(path);
    return path;
}

inline QString logDirectory()
{
    const QString path = QCoreApplication::applicationDirPath() + "/log";
    QDir().mkpath(path);
    return path;
}

inline QString configPath()
{
    return userDirectory() + "/config.ini";
}

inline QString logPath(Category category)
{
    Q_UNUSED(category);
    return logDirectory() + "/csv_viewer.log";
}

inline QString logPathForPart(int partIndex)
{
    if (partIndex <= 0)
        return logDirectory() + "/csv_viewer.log";
    return logDirectory() + QString("/csv_viewer_%1.log").arg(partIndex, 3, 10, QLatin1Char('0'));
}

inline QString settingsPath(Category category)
{
    return "logs/" + QString::fromLatin1(categoryInfo(category).settingsKey);
}

inline bool isEnabled(Category category)
{
    QSettings settings(configPath(), QSettings::IniFormat);
    return settings.value(settingsPath(category), true).toBool();
}

inline void setEnabled(Category category, bool enabled)
{
    QSettings settings(configPath(), QSettings::IniFormat);
    settings.setValue(settingsPath(category), enabled);
    settings.sync();
}

// Start every application session with one bounded, session-local diagnostic log.
inline void initializeSessionFiles()
{
    QSettings settings(configPath(), QSettings::IniFormat);
    for (const auto& info : categories())
    {
        const QString key = "logs/" + QString::fromLatin1(info.settingsKey);
        if (!settings.contains(key))
            settings.setValue(key, true);
    }
    settings.sync();

    // Remove the two legacy locations so all diagnostics live under log/.
    QFile::remove(userDirectory() + "/shutdown_trace.txt");
    QFile::remove(userDirectory() + "/plot_gl_trace.txt");

    QDir dir(logDirectory());
    const QStringList oldParts = dir.entryList(
        QStringList{ QStringLiteral("csv_viewer.log"), QStringLiteral("csv_viewer_*.log") },
        QDir::Files);
    for (const QString& fileName : oldParts)
        QFile::remove(dir.filePath(fileName));
}

inline void write(Category category, const QString& message)
{
    if (!isEnabled(category))
        return;

    static QMutex mutex;
    const QMutexLocker locker(&mutex);

    const QString line = QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
        + " | pid=" + QString::number(QCoreApplication::applicationPid())
        + " tid=0x" + QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16)
        + " [" + QString::fromLatin1(categoryInfo(category).settingsKey) + "]"
        + " | " + message + "\n";
    const QByteArray encodedLine = line.toUtf8();

    // Keep every individual part strictly below 8 MiB and continue in a
    // lexicographically sortable numbered part when the active file is full.
    constexpr qint64 kMaxLogBytes = 8LL * 1024LL * 1024LL - 4096LL;
    if (encodedLine.size() >= kMaxLogBytes)
        return;

    QLockFile interModuleLock(logDirectory() + "/csv_viewer.lock");
    interModuleLock.setStaleLockTime(30000);
    if (!interModuleLock.tryLock(1000))
        return;

    int partIndex = 0;
    QString selectedPath;
    bool newPart = false;
    while (true)
    {
        selectedPath = logPathForPart(partIndex);
        const QFileInfo info(selectedPath);
        const qint64 existingSize = info.exists() ? info.size() : 0;
        newPart = existingSize == 0;

        QByteArray continuation;
        if (partIndex > 0 && newPart)
        {
            continuation = (QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
                + QString(" [logger] | LOG_CONTINUED part=%1 previous=%2\n")
                      .arg(partIndex)
                      .arg(QFileInfo(logPathForPart(partIndex - 1)).fileName())).toUtf8();
        }

        if (existingSize + continuation.size() + encodedLine.size() < kMaxLogBytes)
            break;
        ++partIndex;
    }

    QFile file(selectedPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    if (partIndex > 0 && newPart)
    {
        const QByteArray continuation = (QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
            + QString(" [logger] | LOG_CONTINUED part=%1 previous=%2\n")
                  .arg(partIndex)
                  .arg(QFileInfo(logPathForPart(partIndex - 1)).fileName())).toUtf8();
        file.write(continuation);
    }
    file.write(encodedLine);
    file.flush();
}

inline void writeRateLimited(Category category,
                             const QString& key,
                             const QString& message,
                             qint64 minimumIntervalMs = 1000)
{
    static QMutex rateMutex;
    static QHash<QString, qint64> lastWrittenMs;
    static QHash<QString, int> suppressedCounts;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    int suppressed = 0;
    {
        const QMutexLocker locker(&rateMutex);
        const qint64 last = lastWrittenMs.value(key, 0);
        if (last > 0 && now - last < minimumIntervalMs)
        {
            suppressedCounts[key] = suppressedCounts.value(key, 0) + 1;
            return;
        }
        lastWrittenMs[key] = now;
        suppressed = suppressedCounts.take(key);
    }

    write(category, suppressed > 0
          ? message + QString(" | suppressedSincePrevious=%1").arg(suppressed)
          : message);
}

} // namespace trace
} // namespace viewer
