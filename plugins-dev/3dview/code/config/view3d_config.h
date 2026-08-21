#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>

namespace view3d
{

enum class JointType
{
    Revolute,
    Prismatic
};

struct JointVariableConfig
{
    QString name;
    JointType type = JointType::Revolute;
};

struct PresetConfig
{
    QString jointTrack;
    QStringList tcpTracks;
    QString poseTrack;
};

struct ModelLinkConfig
{
    QString stlFile;
    QVector3D translation;
    QVector3D eulerZyx;
};

struct ModelConfig
{
    QString directory;
    QString baseStl;
    QVector<ModelLinkConfig> links;

    bool isEmpty() const noexcept
    {
        return directory.isEmpty() && baseStl.isEmpty() && links.isEmpty();
    }
};

struct View3DConfig
{
    int version = 1;
    QString timeColumn;
    double sampleRate = 1.0;
    QString positionUnit = QStringLiteral("mm");
    QString angleUnit = QStringLiteral("deg");
    QString timeUnit = QStringLiteral("s");
    QMap<QString, QVector<JointVariableConfig>> jointTracks;
    QMap<QString, QStringList> tcpTracks;
    QMap<QString, PresetConfig> presets;
    QString defaultPreset;
    ModelConfig model;
};

class ConfigLoader final
{
public:
    static bool loadFile(
        const QString& filePath, View3DConfig* config, QString* error = nullptr);
    static bool parse(
        const QByteArray& json, View3DConfig* config, QString* error = nullptr);
};

QString jointTypeName(JointType type);

} // namespace view3d
