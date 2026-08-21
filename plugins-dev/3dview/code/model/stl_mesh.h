#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <QVector3D>

namespace view3d
{

struct MeshVertex
{
    QVector3D position;
    QVector3D normal;
};

struct StlMesh
{
    QVector<MeshVertex> vertices;
    QVector3D boundsMin;
    QVector3D boundsMax;
    bool hasBounds = false;

    bool isEmpty() const noexcept { return vertices.isEmpty(); }
    qsizetype triangleCount() const noexcept { return vertices.size() / 3; }
};

class StlLoader final
{
public:
    static bool loadFile(
        const QString& filePath, StlMesh* mesh, QString* error = nullptr);
    static bool parse(
        const QByteArray& bytes, StlMesh* mesh, QString* error = nullptr);
};

} // namespace view3d
