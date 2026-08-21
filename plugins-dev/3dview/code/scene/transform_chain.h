#pragma once

#include "config/view3d_config.h"

#include <QMatrix4x4>
#include <QVector>

namespace view3d
{

QMatrix4x4 makeZyxTransform(
    const QVector3D& translation,
    const QVector3D& eulerZyx,
    bool anglesAreRadians);

QMatrix4x4 makeTcpPoseTransform(
    const QVector3D& position,
    const QVector3D& eulerZyx,
    bool anglesAreRadians);

QVector<QMatrix4x4> evaluateJointChain(
    const QVector<JointVariableConfig>& joints,
    const QVector<ModelLinkConfig>& links,
    const QVector<double>& values,
    bool anglesAreRadians);

} // namespace view3d
