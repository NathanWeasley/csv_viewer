#pragma once

#include "code_viewer/base/base_def.h"
#include "code_qcp/qcustomplot.h"

#include <QColor>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QVector>
#include <QVector2D>

namespace viewer
{

// OpenGL 散点渲染器：只替换最重的散点绘制路径。
// 线型、图例和不支持的散点形状仍走原来的 QPainter 逻辑。
class VIEWER_API QCPColumnGraphOpenGLPainter
{
public:
    QCPColumnGraphOpenGLPainter();
    ~QCPColumnGraphOpenGLPainter();

    bool drawScatterPlot(QCPPainter* painter,
                         const QVector<QPointF>& scatters,
                         const QCPScatterStyle& style,
                         const QPen& defaultPen,
                         const QRect& clipRect,
                         const QSize& viewportSize,
                         double devicePixelRatio) const;

private:
    enum class ShapeMode
    {
        Disc,
        Circle,
        Square,
        Diamond
    };

    struct GpuState
    {
        ShapeMode shape = ShapeMode::Disc;
        QColor strokeColor;
        QColor fillColor;
        float size = 0.0f;
        float strokeWidth = 0.0f;
    };

    bool ensureProgram(QOpenGLShaderProgram** program) const;
    bool resolveState(const QCPScatterStyle& style,
                      const QPen& defaultPen,
                      GpuState* state) const;
    void buildUploadBuffer(const QVector<QPointF>& scatters) const;

private:
    mutable QVector<QVector2D> m_uploadBuffer;
};

} // namespace viewer
