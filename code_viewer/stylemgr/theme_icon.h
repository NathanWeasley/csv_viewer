#pragma once

#include "code_viewer/base/base_def.h"

#include <QIcon>
#include <QString>

namespace viewer::theme
{

// 使用与 Viewer 主工具栏一致的标记色规则生成主题图标：
// #000000（含 SVG 默认黑色填充）表示前景/描边色，#FFFFFF 表示背景/填充色，
// 其他颜色保持不变。
VIEWER_API QString normalizeSvgColors(const QString& svgText, bool darkMode);

VIEWER_API QIcon createSvgIcon(
    const QString& svgText,
    bool darkMode,
    int logicalSize = 36,
    qreal devicePixelRatio = 0.0,
    QString* errorMessage = nullptr);

// sourcePath 可以是 Qt 资源路径（:/...）或普通文件路径。
VIEWER_API QIcon loadSvgIcon(
    const QString& sourcePath,
    bool darkMode,
    int logicalSize = 36,
    qreal devicePixelRatio = 0.0,
    QString* errorMessage = nullptr);

} // namespace viewer::theme
