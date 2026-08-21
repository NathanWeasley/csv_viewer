#include "trajectory_data.h"

#include <QtGlobal>

#include <algorithm>
#include <limits>

namespace view3d
{
namespace
{

qsizetype safeSampleCount(
    qsizetype initial,
    const viewer::plugin::ColumnView& column)
{
    if (!column.isValid())
        return 0;
    return std::min(initial, column.size);
}

qsizetype clampFrame(qsizetype frame, qsizetype count)
{
    return count > 0 ? qBound<qsizetype>(0, frame, count - 1) : 0;
}

} // namespace

bool TrajectoryRepository::rebuild(
    const View3DConfig& config,
    viewer::plugin::DataSnapshotPtr snapshot,
    QStringList* diagnostics)
{
    clear();
    if (!snapshot)
    {
        if (diagnostics)
            diagnostics->push_back(QStringLiteral("Viewer has no loaded data snapshot."));
        return false;
    }

    m_snapshot = std::move(snapshot);
    m_frameCount = m_snapshot->rowCount();
    m_sampleRate = config.sampleRate;
    m_timeScale = config.timeUnit == QStringLiteral("ms") ? 0.001 : 1.0;
    if (!config.timeColumn.isEmpty())
    {
        m_time = m_snapshot->column(config.timeColumn);
        if (!m_time.isValid() && diagnostics)
            diagnostics->push_back(
                QStringLiteral("Time column '%1' is unavailable; sampleRate is used.")
                    .arg(config.timeColumn));
    }

    for (auto it = config.jointTracks.constBegin(); it != config.jointTracks.constEnd(); ++it)
    {
        MappedJointTrack mapped;
        mapped.name = it.key();
        mapped.sampleCount = m_frameCount;
        QStringList missing;
        for (const JointVariableConfig& variable : it.value())
        {
            MappedJointChannel channel;
            channel.name = variable.name;
            channel.type = variable.type;
            channel.values = m_snapshot->column(variable.name);
            if (!channel.values.isValid())
                missing.push_back(variable.name);
            mapped.sampleCount = safeSampleCount(mapped.sampleCount, channel.values);
            mapped.channels.push_back(channel);
        }
        if (missing.isEmpty() && mapped.sampleCount > 0)
            m_jointTracks.insert(mapped.name, mapped);
        else if (diagnostics)
            diagnostics->push_back(
                QStringLiteral("Joint track '%1' is unavailable; missing columns: %2")
                    .arg(mapped.name, missing.join(QStringLiteral(", "))));
    }

    for (auto it = config.tcpTracks.constBegin(); it != config.tcpTracks.constEnd(); ++it)
    {
        MappedTcpTrack mapped;
        mapped.name = it.key();
        mapped.variableNames = it.value();
        mapped.sampleCount = m_frameCount;
        QStringList missing;
        for (size_t index = 0; index < mapped.values.size(); ++index)
        {
            const QString& variable = mapped.variableNames.at(static_cast<qsizetype>(index));
            mapped.values[index] = m_snapshot->column(variable);
            if (!mapped.values[index].isValid())
                missing.push_back(variable);
            mapped.sampleCount = safeSampleCount(mapped.sampleCount, mapped.values[index]);
        }
        if (missing.isEmpty() && mapped.sampleCount > 0)
            m_tcpTracks.insert(mapped.name, mapped);
        else if (diagnostics)
            diagnostics->push_back(
                QStringLiteral("TCP track '%1' is unavailable; missing columns: %2")
                    .arg(mapped.name, missing.join(QStringLiteral(", "))));
    }
    return !m_jointTracks.isEmpty() || !m_tcpTracks.isEmpty();
}

void TrajectoryRepository::clear()
{
    m_snapshot.reset();
    m_jointTracks.clear();
    m_tcpTracks.clear();
    m_time = {};
    m_frameCount = 0;
    m_sampleRate = 1.0;
    m_timeScale = 1.0;
}

const QMap<QString, MappedJointTrack>& TrajectoryRepository::jointTracks() const noexcept
{
    return m_jointTracks;
}

const QMap<QString, MappedTcpTrack>& TrajectoryRepository::tcpTracks() const noexcept
{
    return m_tcpTracks;
}

const MappedJointTrack* TrajectoryRepository::jointTrack(const QString& name) const noexcept
{
    const auto it = m_jointTracks.constFind(name);
    return it == m_jointTracks.constEnd() ? nullptr : &it.value();
}

const MappedTcpTrack* TrajectoryRepository::tcpTrack(const QString& name) const noexcept
{
    const auto it = m_tcpTracks.constFind(name);
    return it == m_tcpTracks.constEnd() ? nullptr : &it.value();
}

QVector<double> TrajectoryRepository::jointValues(
    const QString& trackName, qsizetype frame) const
{
    QVector<double> result;
    const MappedJointTrack* track = jointTrack(trackName);
    if (!track || track->sampleCount <= 0)
        return result;
    const qsizetype row = clampFrame(frame, track->sampleCount);
    result.reserve(track->channels.size());
    for (const MappedJointChannel& channel : track->channels)
        result.push_back(channel.values.data[row]);
    return result;
}

QVector3D TrajectoryRepository::tcpPosition(
    const QString& trackName, qsizetype frame) const
{
    const MappedTcpTrack* track = tcpTrack(trackName);
    if (!track || track->sampleCount <= 0)
        return {};
    const qsizetype row = clampFrame(frame, track->sampleCount);
    return QVector3D(
        static_cast<float>(track->values[0].data[row]),
        static_cast<float>(track->values[1].data[row]),
        static_cast<float>(track->values[2].data[row]));
}

QVector3D TrajectoryRepository::tcpEulerZyx(
    const QString& trackName, qsizetype frame) const
{
    const MappedTcpTrack* track = tcpTrack(trackName);
    if (!track || track->sampleCount <= 0)
        return {};
    const qsizetype row = clampFrame(frame, track->sampleCount);
    return QVector3D(
        static_cast<float>(track->values[3].data[row]),
        static_cast<float>(track->values[4].data[row]),
        static_cast<float>(track->values[5].data[row]));
}

qsizetype TrajectoryRepository::frameCount() const noexcept
{
    return m_frameCount;
}

double TrajectoryRepository::timeAt(qsizetype frame) const noexcept
{
    if (m_frameCount <= 0)
        return 0.0;
    const qsizetype row = clampFrame(frame, m_frameCount);
    if (m_time.isValid() && row < m_time.size)
        return m_time.data[row] * m_timeScale;
    return static_cast<double>(row) / m_sampleRate;
}

viewer::plugin::DataSnapshotPtr TrajectoryRepository::snapshot() const noexcept
{
    return m_snapshot;
}

} // namespace view3d
