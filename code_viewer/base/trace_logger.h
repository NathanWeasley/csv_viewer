#pragma once

#include "code_viewer/base/base_def.h"

#include <QCoreApplication>
#include <QDir>
#include <QString>

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

// The implementation lives in Viewer.dll so the executable and Viewer.dll
// share one settings cache, queue and background writer.
VIEWER_API bool isEnabled(Category category);
VIEWER_API void setEnabled(Category category, bool enabled);

// Start a bounded, session-local diagnostic log and cache category settings.
VIEWER_API void initializeSessionFiles();

VIEWER_API void write(Category category, const QString& message);
VIEWER_API void writeRateLimited(Category category,
                                 const QString& key,
                                 const QString& message,
                                 qint64 minimumIntervalMs = 1000);

// Drain the queue and stop the writer after all objects that can log have been
// destroyed.
VIEWER_API void shutdown();

} // namespace trace
} // namespace viewer
