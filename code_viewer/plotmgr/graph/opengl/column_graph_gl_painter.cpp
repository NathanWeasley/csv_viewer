#include "code_viewer/plotmgr/graph/opengl/column_graph_gl_painter.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QTextStream>
#include <QVector4D>
#include <QtGlobal>

namespace viewer
{

namespace
{

constexpr const char* kVertexShader = R"(
attribute vec2 a_position;
uniform vec2 u_viewportSize;
uniform float u_pointSize;

void main()
{
    vec2 pos = a_position;
    vec2 ndc = vec2(
        pos.x / u_viewportSize.x * 2.0 - 1.0,
        1.0 - pos.y / u_viewportSize.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    gl_PointSize = max(u_pointSize, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(
uniform vec4 u_strokeColor;
uniform vec4 u_fillColor;
uniform float u_shapeMode;
uniform float u_strokeWidth;

void main()
{
    vec2 p = gl_PointCoord * 2.0 - vec2(1.0, 1.0);
    float pixelSize = max(gl_PointSize, 1.0);
    float strokeNorm = clamp((2.0 * max(u_strokeWidth, 0.0)) / pixelSize, 0.0, 1.0);
    float dist;

    if (u_shapeMode < 0.5)
        dist = length(p);
    else if (u_shapeMode < 1.5)
        dist = length(p);
    else if (u_shapeMode < 2.5)
        dist = max(abs(p.x), abs(p.y));
    else
        dist = abs(p.x) + abs(p.y);

    if (dist > 1.0)
        discard;

    float inner = max(0.0, 1.0 - strokeNorm);
    bool hasStroke = u_strokeColor.a > 0.0 && u_strokeWidth > 0.0;
    bool hasFill = u_fillColor.a > 0.0;

    if (hasStroke && dist >= inner)
        gl_FragColor = u_strokeColor;
    else if (hasFill)
        gl_FragColor = u_fillColor;
    else if (hasStroke)
        gl_FragColor = u_strokeColor;
    else
        discard;
}
)";

#ifndef GL_PROGRAM_POINT_SIZE
#ifdef GL_VERTEX_PROGRAM_POINT_SIZE
#define GL_PROGRAM_POINT_SIZE GL_VERTEX_PROGRAM_POINT_SIZE
#else
#define GL_PROGRAM_POINT_SIZE 0x8642
#endif
#endif

inline QVector4D toVec4(const QColor& color)
{
    return QVector4D(color.redF(), color.greenF(), color.blueF(), color.alphaF());
}

void appendOpenGlTrace(const QString& message)
{
    const QString userDir = QCoreApplication::applicationDirPath() + "/user";
    QDir().mkpath(userDir);

    QFile file(userDir + "/plot_gl_trace.txt");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;

    QTextStream out(&file);
    out << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
        << " | " << message << "\n";
    out.flush();
}

} // namespace

QCPColumnGraphOpenGLPainter::QCPColumnGraphOpenGLPainter()
{
}

QCPColumnGraphOpenGLPainter::~QCPColumnGraphOpenGLPainter()
{
}

bool QCPColumnGraphOpenGLPainter::drawScatterPlot(QCPPainter* painter,
                                                  const QVector<QPointF>& scatters,
                                                  const QCPScatterStyle& style,
                                                  const QPen& defaultPen,
                                                  const QRect& clipRect,
                                                  const QSize& viewportSize,
                                                  double devicePixelRatio) const
{
    if (!painter || scatters.isEmpty() || clipRect.isEmpty() || viewportSize.isEmpty())
        return false;

    GpuState state;
    if (!resolveState(style, defaultPen, &state))
        return false;

    QOpenGLShaderProgram* program = nullptr;
    if (!ensureProgram(&program) || !program)
        return false;

    auto* context = QOpenGLContext::currentContext();
    auto* gl = context ? context->functions() : nullptr;
    if (!gl)
        return false;

    buildUploadBuffer(scatters);
    const int viewportWidthLogical = qMax(1, viewportSize.width());
    const int viewportHeightLogical = qMax(1, viewportSize.height());
    const int scissorX = qMax(0, qRound(static_cast<double>(clipRect.left()) * devicePixelRatio));
    const int scissorTop = qMax(0, qRound(static_cast<double>(clipRect.top()) * devicePixelRatio));
    const int scissorW = qMax(0, qRound(static_cast<double>(clipRect.width()) * devicePixelRatio));
    const int scissorH = qMax(0, qRound(static_cast<double>(clipRect.height()) * devicePixelRatio));

    painter->beginNativePainting();

    GLint viewport[4] = { 0, 0, 0, 0 };
    gl->glGetIntegerv(GL_VIEWPORT, viewport);
    const int viewportHeightPx = qMax(1, viewport[3]);
    const int scissorY = qMax(0, viewportHeightPx - (scissorTop + scissorH));
    if (scissorW <= 0 || scissorH <= 0)
    {
        painter->endNativePainting();
        return false;
    }

    gl->glDisable(GL_DEPTH_TEST);
    gl->glEnable(GL_BLEND);
    gl->glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    gl->glEnable(GL_SCISSOR_TEST);
    gl->glScissor(scissorX, scissorY, scissorW, scissorH);
    gl->glEnable(GL_PROGRAM_POINT_SIZE);

    if (m_vertexBufferContext != context)
    {
        if (m_vertexBuffer && m_vertexBuffer->isCreated())
            m_vertexBuffer->destroy();
        m_vertexBuffer.reset(new QOpenGLBuffer(QOpenGLBuffer::VertexBuffer));
        m_vertexBufferContext = context;
        m_vertexBufferCapacity = 0;
        m_loggedGpuState = false;
    }

    if (!m_vertexBuffer || (!m_vertexBuffer->isCreated() && !m_vertexBuffer->create()))
    {
        gl->glDisable(GL_PROGRAM_POINT_SIZE);
        gl->glDisable(GL_SCISSOR_TEST);
        gl->glDisable(GL_BLEND);
        painter->endNativePainting();
        return false;
    }

    const int uploadBytes = m_uploadBuffer.size() * static_cast<int>(sizeof(QVector2D));
    m_vertexBuffer->setUsagePattern(QOpenGLBuffer::StreamDraw);
    if (!m_vertexBuffer->bind())
    {
        gl->glDisable(GL_PROGRAM_POINT_SIZE);
        gl->glDisable(GL_SCISSOR_TEST);
        gl->glDisable(GL_BLEND);
        painter->endNativePainting();
        return false;
    }

    if (uploadBytes > m_vertexBufferCapacity)
    {
        m_vertexBuffer->allocate(m_uploadBuffer.constData(), uploadBytes);
        m_vertexBufferCapacity = uploadBytes;
    }
    else
    {
        m_vertexBuffer->write(0, m_uploadBuffer.constData(), uploadBytes);
    }

    if (!m_loggedGpuState)
    {
        appendOpenGlTrace(QString("ScatterGL context=0x%1 viewportLogical=%2x%3 viewportPx=%4x%5 clip=%6,%7 %8x%9 dpr=%10 vboCapacityKiB=%11")
                          .arg(QString::number(reinterpret_cast<quintptr>(context), 16))
                          .arg(viewportWidthLogical)
                          .arg(viewportHeightLogical)
                          .arg(viewport[2])
                          .arg(viewport[3])
                          .arg(clipRect.x())
                          .arg(clipRect.y())
                          .arg(clipRect.width())
                          .arg(clipRect.height())
                          .arg(QString::number(devicePixelRatio, 'f', 2))
                          .arg(QString::number(static_cast<double>(m_vertexBufferCapacity) / 1024.0, 'f', 1)));
        m_loggedGpuState = true;
    }

    program->bind();
    program->setUniformValue("u_viewportSize", QVector2D(viewportWidthLogical, viewportHeightLogical));
    program->setUniformValue("u_pointSize", state.size);
    program->setUniformValue("u_strokeColor", toVec4(state.strokeColor));
    program->setUniformValue("u_fillColor", toVec4(state.fillColor));
    program->setUniformValue("u_strokeWidth", state.strokeWidth);

    float shapeMode = 0.0f;
    switch (state.shape)
    {
    case ShapeMode::Disc:    shapeMode = 0.0f; break;
    case ShapeMode::Circle:  shapeMode = 1.0f; break;
    case ShapeMode::Square:  shapeMode = 2.0f; break;
    case ShapeMode::Diamond: shapeMode = 3.0f; break;
    }
    program->setUniformValue("u_shapeMode", shapeMode);

    const int positionLocation = program->attributeLocation("a_position");
    if (positionLocation < 0)
    {
        m_vertexBuffer->release();
        program->release();
        gl->glDisable(GL_PROGRAM_POINT_SIZE);
        gl->glDisable(GL_SCISSOR_TEST);
        gl->glDisable(GL_BLEND);
        painter->endNativePainting();
        return false;
    }

    program->enableAttributeArray(positionLocation);
    program->setAttributeBuffer(positionLocation, GL_FLOAT, 0, 2, static_cast<int>(sizeof(QVector2D)));

    gl->glDrawArrays(GL_POINTS, 0, scatters.size());

    program->disableAttributeArray(positionLocation);
    m_vertexBuffer->release();
    program->release();

    gl->glDisable(GL_PROGRAM_POINT_SIZE);
    gl->glDisable(GL_SCISSOR_TEST);
    gl->glDisable(GL_BLEND);

    painter->endNativePainting();
    return true;
}

bool QCPColumnGraphOpenGLPainter::ensureProgram(QOpenGLShaderProgram** program) const
{
    if (!program)
        return false;

    auto* context = QOpenGLContext::currentContext();
    if (!context)
        return false;

    static QHash<QOpenGLContext*, QOpenGLShaderProgram*> s_programs;
    auto it = s_programs.find(context);
    if (it == s_programs.end())
    {
        QOpenGLShaderProgram* shaderProgram = new QOpenGLShaderProgram();
        if (!shaderProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader) ||
            !shaderProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader) ||
            !shaderProgram->link())
        {
            delete shaderProgram;
            return false;
        }
        it = s_programs.insert(context, shaderProgram);
    }

    *program = it.value();
    return true;
}

bool QCPColumnGraphOpenGLPainter::resolveState(const QCPScatterStyle& style,
                                               const QPen& defaultPen,
                                               GpuState* state) const
{
    if (!state)
        return false;

    switch (style.shape())
    {
    case QCPScatterStyle::ssDisc:
        state->shape = ShapeMode::Disc;
        break;
    case QCPScatterStyle::ssCircle:
        state->shape = ShapeMode::Circle;
        break;
    case QCPScatterStyle::ssSquare:
        state->shape = ShapeMode::Square;
        break;
    case QCPScatterStyle::ssDiamond:
        state->shape = ShapeMode::Diamond;
        break;
    default:
        return false;
    }

    const QPen strokePen = style.isPenDefined() ? style.pen() : defaultPen;
    const double strokeWidth = strokePen.style() == Qt::NoPen
        ? 0.0
        : (qFuzzyIsNull(strokePen.widthF()) ? 1.0 : qMax(0.0, strokePen.widthF()));

    state->strokeColor = (strokePen.style() == Qt::NoPen) ? QColor(0, 0, 0, 0) : strokePen.color();
    state->fillColor = style.brush().style() == Qt::NoBrush ? QColor(0, 0, 0, 0) : style.brush().color();
    state->size = static_cast<float>(style.size());
    state->strokeWidth = static_cast<float>(strokeWidth);

    if (style.shape() == QCPScatterStyle::ssDisc && state->fillColor.alpha() == 0 && state->strokeColor.alpha() > 0)
        state->fillColor = state->strokeColor;

    return state->size > 0.0f;
}

void QCPColumnGraphOpenGLPainter::buildUploadBuffer(const QVector<QPointF>& scatters) const
{
    m_uploadBuffer.resize(scatters.size());
    for (int i = 0; i < scatters.size(); ++i)
    {
        const QPointF pt = scatters.at(i);
        m_uploadBuffer[i] = QVector2D(static_cast<float>(pt.x()), static_cast<float>(pt.y()));
    }
}

} // namespace viewer
