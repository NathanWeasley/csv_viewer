#pragma once

#include "config/view3d_config.h"
#include "viewer_plugin/viewer_plugin_sdk.h"

#include <QMap>
#include <QStringList>
#include <QVector>
#include <QVector3D>

#include <array>

namespace view3d
{

struct MappedJointChannel
{
    QString name;
    JointType type = JointType::Revolute;
    viewer::plugin::ColumnView values;
};

struct MappedJointTrack
{
    QString name;
    QVector<MappedJointChannel> channels;
    qsizetype sampleCount = 0;
};

struct MappedTcpTrack
{
    QString name;
    QStringList variableNames;
    std::array<viewer::plugin::ColumnView, 6> values;
    qsizetype sampleCount = 0;
};

class TrajectoryRepository final
{
public:
    bool rebuild(
        const View3DConfig& config,
        viewer::plugin::DataSnapshotPtr snapshot,
        QStringList* diagnostics = nullptr);
    void clear();

    const QMap<QString, MappedJointTrack>& jointTracks() const noexcept;
    const QMap<QString, MappedTcpTrack>& tcpTracks() const noexcept;
    const MappedJointTrack* jointTrack(const QString& name) const noexcept;
    const MappedTcpTrack* tcpTrack(const QString& name) const noexcept;

    QVector<double> jointValues(const QString& trackName, qsizetype frame) const;
    QVector3D tcpPosition(const QString& trackName, qsizetype frame) const;
    QVector3D tcpEulerZyx(const QString& trackName, qsizetype frame) const;
    qsizetype frameCount() const noexcept;
    viewer::plugin::DataSnapshotPtr snapshot() const noexcept;

private:
    viewer::plugin::DataSnapshotPtr m_snapshot;
    QMap<QString, MappedJointTrack> m_jointTracks;
    QMap<QString, MappedTcpTrack> m_tcpTracks;
    qsizetype m_frameCount = 0;
};

} // namespace view3d
