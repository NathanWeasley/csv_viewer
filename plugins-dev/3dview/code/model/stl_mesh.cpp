#include "stl_mesh.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace view3d
{
namespace
{

bool fail(QString* error, const QString& message)
{
    if (error)
        *error = message;
    return false;
}

bool finite(const QVector3D& value)
{
    return std::isfinite(value.x())
        && std::isfinite(value.y())
        && std::isfinite(value.z());
}

float readFloat(const char* data)
{
    const quint32 bits = qFromLittleEndian<quint32>(
        reinterpret_cast<const uchar*>(data));
    float result = 0.0f;
    static_assert(sizeof(result) == sizeof(bits));
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}

QVector3D readVector(const char* data)
{
    return QVector3D(
        readFloat(data), readFloat(data + 4), readFloat(data + 8));
}

void includePoint(StlMesh* mesh, const QVector3D& point)
{
    if (!mesh->hasBounds)
    {
        mesh->boundsMin = point;
        mesh->boundsMax = point;
        mesh->hasBounds = true;
        return;
    }
    mesh->boundsMin.setX(std::min(mesh->boundsMin.x(), point.x()));
    mesh->boundsMin.setY(std::min(mesh->boundsMin.y(), point.y()));
    mesh->boundsMin.setZ(std::min(mesh->boundsMin.z(), point.z()));
    mesh->boundsMax.setX(std::max(mesh->boundsMax.x(), point.x()));
    mesh->boundsMax.setY(std::max(mesh->boundsMax.y(), point.y()));
    mesh->boundsMax.setZ(std::max(mesh->boundsMax.z(), point.z()));
}

QVector3D faceNormal(
    const QVector3D& first,
    const QVector3D& second,
    const QVector3D& third)
{
    QVector3D normal = QVector3D::crossProduct(second - first, third - first);
    if (!qFuzzyIsNull(normal.lengthSquared()))
        normal.normalize();
    return normal;
}

bool parseBinary(const QByteArray& bytes, quint32 triangleCount, StlMesh* mesh)
{
    mesh->vertices.reserve(static_cast<qsizetype>(triangleCount) * 3);
    const char* triangle = bytes.constData() + 84;
    for (quint32 index = 0; index < triangleCount; ++index, triangle += 50)
    {
        QVector3D normal = readVector(triangle);
        const QVector3D first = readVector(triangle + 12);
        const QVector3D second = readVector(triangle + 24);
        const QVector3D third = readVector(triangle + 36);
        if (!finite(first) || !finite(second) || !finite(third))
            return false;
        if (!finite(normal) || qFuzzyIsNull(normal.lengthSquared()))
            normal = faceNormal(first, second, third);
        else
            normal.normalize();
        mesh->vertices.push_back({first, normal});
        mesh->vertices.push_back({second, normal});
        mesh->vertices.push_back({third, normal});
        includePoint(mesh, first);
        includePoint(mesh, second);
        includePoint(mesh, third);
    }
    return !mesh->vertices.isEmpty();
}

bool parseAscii(const QByteArray& bytes, StlMesh* mesh)
{
    QVector3D currentNormal;
    QVector<QVector3D> pendingVertices;
    const QList<QByteArray> lines = bytes.split('\n');
    for (QByteArray line : lines)
    {
        line = line.trimmed().simplified();
        if (line.startsWith("facet normal "))
        {
            const QList<QByteArray> parts = line.split(' ');
            if (parts.size() >= 5)
                currentNormal = QVector3D(
                    parts.at(2).toFloat(), parts.at(3).toFloat(), parts.at(4).toFloat());
        }
        else if (line.startsWith("vertex "))
        {
            const QList<QByteArray> parts = line.split(' ');
            if (parts.size() < 4)
                return false;
            const QVector3D vertex(
                parts.at(1).toFloat(), parts.at(2).toFloat(), parts.at(3).toFloat());
            if (!finite(vertex))
                return false;
            pendingVertices.push_back(vertex);
            if (pendingVertices.size() == 3)
            {
                QVector3D normal = currentNormal;
                if (!finite(normal) || qFuzzyIsNull(normal.lengthSquared()))
                    normal = faceNormal(
                        pendingVertices.at(0), pendingVertices.at(1), pendingVertices.at(2));
                else
                    normal.normalize();
                for (const QVector3D& point : pendingVertices)
                {
                    mesh->vertices.push_back({point, normal});
                    includePoint(mesh, point);
                }
                pendingVertices.clear();
            }
        }
    }
    return !mesh->vertices.isEmpty() && pendingVertices.isEmpty();
}

} // namespace

bool StlLoader::loadFile(
    const QString& filePath, StlMesh* mesh, QString* error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Cannot open STL %1: %2")
                               .arg(filePath, file.errorString()));
    if (!parse(file.readAll(), mesh, error))
    {
        if (error && error->isEmpty())
            *error = QStringLiteral("Invalid STL file: %1").arg(filePath);
        return false;
    }
    return true;
}

bool StlLoader::parse(
    const QByteArray& bytes, StlMesh* mesh, QString* error)
{
    if (!mesh)
        return fail(error, QStringLiteral("The output mesh pointer is null."));
    *mesh = {};
    if (bytes.size() < 15)
        return fail(error, QStringLiteral("STL data is too short."));

    if (bytes.size() >= 84)
    {
        const quint32 triangleCount = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar*>(bytes.constData() + 80));
        const quint64 expected = 84ULL + static_cast<quint64>(triangleCount) * 50ULL;
        if (triangleCount > 0 && expected <= static_cast<quint64>(bytes.size()))
        {
            if (parseBinary(bytes, triangleCount, mesh))
            {
                if (error)
                    error->clear();
                return true;
            }
            *mesh = {};
        }
    }

    if (!parseAscii(bytes, mesh))
        return fail(error, QStringLiteral("STL is neither valid binary nor valid ASCII data."));
    if (error)
        error->clear();
    return true;
}

} // namespace view3d
