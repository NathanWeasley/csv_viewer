#include "code_viewer/base/trace_logger.h"

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QThread>
#include <QWaitCondition>

#include <chrono>
#include <deque>
#include <thread>
#include <utility>
#include <vector>

namespace viewer
{
namespace trace
{
namespace
{

constexpr qint64 kMaxLogBytes = 8LL * 1024LL * 1024LL - 4096LL;
constexpr size_t kMaxQueuedLines = 32768;
constexpr auto kBatchDelay = std::chrono::milliseconds(20);

struct LoggerState
{
    QMutex mutex;
    QWaitCondition workAvailable;
    std::array<bool, 8> enabled{};
    bool settingsLoaded = false;
    bool stopRequested = false;
    bool workerStarted = false;
    std::thread worker;
    std::deque<QByteArray> pendingLines;
    quint64 droppedLines = 0;
    QHash<QString, qint64> lastWrittenMs;
    QHash<QString, int> suppressedCounts;
};

// The logger is explicitly shut down from main(). Leaking this small control
// object avoids static-destruction ordering problems between Qt and Viewer.dll
// if startup exits exceptionally before main reaches the shutdown call.
LoggerState& loggerState()
{
    static LoggerState* state = new LoggerState;
    return *state;
}

size_t categoryIndex(Category category)
{
    const auto& infos = categories();
    for (size_t index = 0; index < infos.size(); ++index)
    {
        if (infos[index].category == category)
            return index;
    }
    return 0;
}

void loadSettings(LoggerState& state, bool createMissingKeys)
{
    QSettings settings(configPath(), QSettings::IniFormat);
    const auto& infos = categories();
    for (size_t index = 0; index < infos.size(); ++index)
    {
        const QString key = settingsPath(infos[index].category);
        if (createMissingKeys && !settings.contains(key))
            settings.setValue(key, true);
        state.enabled[index] = settings.value(key, true).toBool();
    }
    if (createMissingKeys)
        settings.sync();
    state.settingsLoaded = true;
}

QByteArray encodeLine(Category category, const QString& message)
{
    return (QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
        + " | pid=" + QString::number(QCoreApplication::applicationPid())
        + " tid=0x" + QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16)
        + " [" + QString::fromLatin1(categoryInfo(category).settingsKey) + "]"
        + " | " + message + "\n").toUtf8();
}

struct WriterFile
{
    QFile file;
    int partIndex = 0;
    qint64 fileSize = 0;
};

bool openPart(WriterFile& writer, int partIndex)
{
    writer.file.close();
    writer.file.setFileName(logPathForPart(partIndex));
    if (!writer.file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return false;

    writer.partIndex = partIndex;
    writer.fileSize = writer.file.size();
    return true;
}

bool ensureLogFileOpen(WriterFile& writer, qsizetype pendingBytes)
{
    if (!writer.file.isOpen())
    {
        int partIndex = 0;
        while (QFileInfo(logPathForPart(partIndex)).size() + pendingBytes >= kMaxLogBytes)
            ++partIndex;
        if (!openPart(writer, partIndex))
            return false;
    }

    if (writer.fileSize + pendingBytes < kMaxLogBytes)
        return true;

    const int nextPart = writer.partIndex + 1;
    if (!openPart(writer, nextPart))
        return false;

    const QByteArray continuation =
        (QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
         + QString(" [logger] | LOG_CONTINUED part=%1 previous=%2\n")
               .arg(nextPart)
               .arg(QFileInfo(logPathForPart(nextPart - 1)).fileName())).toUtf8();
    const qint64 written = writer.file.write(continuation);
    if (written > 0)
        writer.fileSize += written;
    return true;
}

void writeLine(WriterFile& writer, const QByteArray& line)
{
    if (line.size() >= kMaxLogBytes || !ensureLogFileOpen(writer, line.size()))
        return;

    const qint64 written = writer.file.write(line);
    if (written > 0)
        writer.fileSize += written;
}

void writerMain(LoggerState* state)
{
    WriterFile writer;
    std::vector<QByteArray> batch;

    while (true)
    {
        {
            QMutexLocker locker(&state->mutex);
            while (state->pendingLines.empty() && !state->stopRequested)
                state->workAvailable.wait(&state->mutex);
            if (state->pendingLines.empty() && state->stopRequested)
                break;
        }

        // Coalesce bursts such as plot creation into one write/flush batch.
        std::this_thread::sleep_for(kBatchDelay);

        quint64 dropped = 0;
        {
            const QMutexLocker locker(&state->mutex);
            batch.clear();
            batch.reserve(state->pendingLines.size() + (state->droppedLines > 0 ? 1 : 0));
            while (!state->pendingLines.empty())
            {
                batch.push_back(std::move(state->pendingLines.front()));
                state->pendingLines.pop_front();
            }
            dropped = std::exchange(state->droppedLines, 0);
        }

        if (dropped > 0)
        {
            batch.push_back(encodeLine(
                Category::Operation,
                QString("logger queue overflow; droppedLines=%1").arg(dropped)));
        }

        for (const QByteArray& line : batch)
            writeLine(writer, line);
        if (writer.file.isOpen())
            writer.file.flush();
    }

    if (writer.file.isOpen())
    {
        writer.file.flush();
        writer.file.close();
    }
}

void startWorkerLocked(LoggerState& state)
{
    if (state.workerStarted)
        return;
    state.stopRequested = false;
    state.workerStarted = true;
    state.worker = std::thread(writerMain, &state);
}

void enqueueLocked(LoggerState& state, QByteArray line)
{
    if (line.size() >= kMaxLogBytes)
        return;
    startWorkerLocked(state);

    if (state.pendingLines.size() >= kMaxQueuedLines)
    {
        ++state.droppedLines;
        return;
    }

    const bool wasEmpty = state.pendingLines.empty();
    state.pendingLines.push_back(std::move(line));
    if (wasEmpty)
        state.workAvailable.wakeOne();
}

} // namespace

bool isEnabled(Category category)
{
    LoggerState& state = loggerState();
    const QMutexLocker locker(&state.mutex);
    if (!state.settingsLoaded)
        loadSettings(state, false);
    return state.enabled[categoryIndex(category)];
}

void setEnabled(Category category, bool enabled)
{
    LoggerState& state = loggerState();
    {
        const QMutexLocker locker(&state.mutex);
        if (!state.settingsLoaded)
            loadSettings(state, false);
        state.enabled[categoryIndex(category)] = enabled;
    }

    // Settings changes are rare and may remain synchronous. The hot write path
    // only consults the in-memory cache above.
    QSettings settings(configPath(), QSettings::IniFormat);
    settings.setValue(settingsPath(category), enabled);
    settings.sync();
}

void initializeSessionFiles()
{
    // Safe even if a caller deliberately starts a second session in-process.
    shutdown();

    LoggerState& state = loggerState();
    const QMutexLocker locker(&state.mutex);
    state.pendingLines.clear();
    state.droppedLines = 0;
    state.lastWrittenMs.clear();
    state.suppressedCounts.clear();
    loadSettings(state, true);

    // Remove the two legacy locations so all UI diagnostics live under log/.
    QFile::remove(userDirectory() + "/shutdown_trace.txt");
    QFile::remove(userDirectory() + "/plot_gl_trace.txt");

    QDir dir(logDirectory());
    const QStringList oldParts = dir.entryList(
        QStringList{ QStringLiteral("csv_viewer.log"), QStringLiteral("csv_viewer_*.log") },
        QDir::Files);
    for (const QString& fileName : oldParts)
        QFile::remove(dir.filePath(fileName));
}

void write(Category category, const QString& message)
{
    LoggerState& state = loggerState();
    const QMutexLocker locker(&state.mutex);
    if (!state.settingsLoaded)
        loadSettings(state, false);
    if (!state.enabled[categoryIndex(category)])
        return;
    enqueueLocked(state, encodeLine(category, message));
}

void writeRateLimited(Category category,
                      const QString& key,
                      const QString& message,
                      qint64 minimumIntervalMs)
{
    LoggerState& state = loggerState();
    const QMutexLocker locker(&state.mutex);
    if (!state.settingsLoaded)
        loadSettings(state, false);
    if (!state.enabled[categoryIndex(category)])
        return;

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 last = state.lastWrittenMs.value(key, 0);
    if (last > 0 && now - last < minimumIntervalMs)
    {
        state.suppressedCounts[key] = state.suppressedCounts.value(key, 0) + 1;
        return;
    }

    state.lastWrittenMs[key] = now;
    const int suppressed = state.suppressedCounts.take(key);
    enqueueLocked(state, encodeLine(category, suppressed > 0
        ? message + QString(" | suppressedSincePrevious=%1").arg(suppressed)
        : message));
}

void shutdown()
{
    LoggerState& state = loggerState();
    {
        const QMutexLocker locker(&state.mutex);
        if (!state.workerStarted)
            return;
        state.stopRequested = true;
        state.workAvailable.wakeOne();
    }

    if (state.worker.joinable())
        state.worker.join();

    const QMutexLocker locker(&state.mutex);
    state.workerStarted = false;
    state.stopRequested = false;
}

} // namespace trace
} // namespace viewer
