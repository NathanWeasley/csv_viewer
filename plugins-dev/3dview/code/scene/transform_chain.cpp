#include "transform_chain.h"

#include <QtMath>

#include <algorithm>

namespace view3d
{
namespace
{

float degrees(float value, bool radians)
{
    return radians ? static_cast<float>(qRadiansToDegrees(value)) : value;
}

} // namespace

QMatrix4x4 makeZyxTransform(
    const QVector3D& translation,
    const QVector3D& eulerZyx,
    bool anglesAreRadians)
{
    QMatrix4x4 result;
    result.translate(translation);
    // JSON stores [rx, ry, rz]; applying Z, then Y, then X gives Rz * Ry * Rx.
    result.rotate(degrees(eulerZyx.z(), anglesAreRadians), 0.0f, 0.0f, 1.0f);
    result.rotate(degrees(eulerZyx.y(), anglesAreRadians), 0.0f, 1.0f, 0.0f);
    result.rotate(degrees(eulerZyx.x(), anglesAreRadians), 1.0f, 0.0f, 0.0f);
    return result;
}

QMatrix4x4 makeTcpPoseTransform(
    const QVector3D& position,
    const QVector3D& eulerZyx,
    bool anglesAreRadians)
{
    return makeZyxTransform(position, eulerZyx, anglesAreRadians);
}

QVector<QMatrix4x4> evaluateJointChain(
    const QVector<JointVariableConfig>& joints,
    const QVector<ModelLinkConfig>& links,
    const QVector<double>& values,
    bool anglesAreRadians)
{
    const qsizetype count = std::min(joints.size(), values.size());
    QVector<QMatrix4x4> worldTransforms;
    worldTransforms.reserve(count);
    QMatrix4x4 parentWorld;

    for (qsizetype index = 0; index < count; ++index)
    {
        QMatrix4x4 fixed;
        if (index < links.size())
            fixed = makeZyxTransform(
                links.at(index).translation,
                links.at(index).eulerZyx,
                anglesAreRadians);

        QMatrix4x4 dynamic;
        if (joints.at(index).type == JointType::Revolute)
        {
            dynamic.rotate(
                degrees(static_cast<float>(values.at(index)), anglesAreRadians),
                0.0f, 0.0f, 1.0f);
        }
        else
        {
            dynamic.translate(0.0f, 0.0f, static_cast<float>(values.at(index)));
        }

        const QMatrix4x4 world = parentWorld * fixed * dynamic;
        worldTransforms.push_back(world);
        parentWorld = world;
    }
    return worldTransforms;
}

} // namespace view3d
