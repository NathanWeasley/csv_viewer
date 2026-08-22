#include "view3d_scene.h"

#include <QMouseEvent>
#include <QSizePolicy>
#include <QSurfaceFormat>
#include <QWheelEvent>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace view3d
{
namespace
{

QVector3D rgb(const QColor& color)
{
    return QVector3D(
        static_cast<float>(color.redF()),
        static_cast<float>(color.greenF()),
        static_cast<float>(color.blueF()));
}

bool finite(const QVector3D& point)
{
    return std::isfinite(point.x())
        && std::isfinite(point.y())
        && std::isfinite(point.z());
}

} // namespace

View3DScene::View3DScene(QWidget* parent)
    : QOpenGLWidget(parent)
{
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    setFormat(format);
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(200, 150);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

View3DScene::~View3DScene()
{
    if (context())
    {
        makeCurrent();
        m_vertexArray.destroy();
        m_vertexBuffer.destroy();
        doneCurrent();
    }
}

void View3DScene::setTrajectories(const QVector<RenderTrajectory>& trajectories)
{
    m_trajectories = trajectories;
    updateBounds();
    if (!m_cameraInitialized)
        fitBounds();
    update();
}

void View3DScene::setTrajectoryStyle(
    const QString& name, const TrajectoryStyle& style)
{
    for (RenderTrajectory& trajectory : m_trajectories)
    {
        if (trajectory.name == name)
        {
            trajectory.style = style;
            update();
            return;
        }
    }
}

void View3DScene::setTrajectoryDisplay(
    TrajectoryDisplayMode mode,
    qsizetype currentFrame,
    qsizetype localRadius)
{
    m_trajectoryDisplayMode = mode;
    m_currentFrame = std::max<qsizetype>(0, currentFrame);
    m_localRadius = std::max<qsizetype>(0, localRadius);
    update();
}

void View3DScene::setMeshes(const QVector<RenderMesh>& meshes)
{
    m_meshes = meshes;
    updateBounds();
    if (!m_cameraInitialized)
        fitBounds();
    update();
}

void View3DScene::setMeshTransforms(
    const QVector<QMatrix4x4>& linkTransforms)
{
    for (RenderMesh& mesh : m_meshes)
    {
        if (mesh.linkIndex >= 0 && mesh.linkIndex < linkTransforms.size())
            mesh.transform = linkTransforms.at(mesh.linkIndex);
        else if (mesh.linkIndex >= 0)
            mesh.transform.setToIdentity();
    }
    update();
}

void View3DScene::setTcpPose(const QMatrix4x4& pose, bool visible)
{
    m_tcpPose = pose;
    m_tcpPoseVisible = visible;
    update();
}

void View3DScene::setJointFrames(const QVector<QMatrix4x4>& frames)
{
    m_jointFrames = frames;
    update();
}

void View3DScene::resetCamera()
{
    updateBounds();
    fitBounds();
    update();
}

void View3DScene::initializeGL()
{
    initializeOpenGLFunctions();
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    static const char* vertexShader = R"glsl(
        #version 330 core
        layout(location = 0) in vec3 inPosition;
        layout(location = 1) in vec3 inColor;
        layout(location = 2) in float inDistance;
        uniform mat4 mvp;
        out vec3 vertexColor;
        out float pathDistance;
        void main()
        {
            gl_Position = mvp * vec4(inPosition, 1.0);
            vertexColor = inColor;
            pathDistance = inDistance;
        }
    )glsl";
    static const char* fragmentShader = R"glsl(
        #version 330 core
        in vec3 vertexColor;
        in float pathDistance;
        uniform int linePattern;
        uniform float patternScale;
        out vec4 fragmentColor;
        void main()
        {
            if (linePattern == 1 && mod(pathDistance / patternScale, 2.0) > 1.0)
                discard;
            if (linePattern == 2 && mod(pathDistance / patternScale, 2.5) > 0.55)
                discard;
            fragmentColor = vec4(vertexColor, 1.0);
        }
    )glsl";

    const bool shadersOk = m_program.addShaderFromSourceCode(
                               QOpenGLShader::Vertex, vertexShader)
        && m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShader)
        && m_program.link();
    if (!shadersOk)
        return;

    if (!m_vertexArray.create() || !m_vertexBuffer.create())
        return;
    m_glReady = true;
}

void View3DScene::resizeGL(int, int)
{
}

void View3DScene::paintGL()
{
    glClearColor(0.075f, 0.085f, 0.105f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_glReady)
        return;

    m_program.bind();
    m_program.setUniformValue("mvp", viewProjection());
    m_program.setUniformValue("patternScale", std::max(0.001f, m_sceneRadius * 0.025f));

    const float gridStep = niceGridStep();
    drawGrid(gridStep * 10.0f, gridStep);

    QMatrix4x4 identity;
    drawAxes(identity, std::max(gridStep * 1.5f, m_sceneRadius * 0.15f), 2.0f);
    for (const RenderMesh& mesh : m_meshes)
        drawMesh(mesh);
    for (const RenderTrajectory& trajectory : m_trajectories)
        drawPolyline(trajectory);
    for (const QMatrix4x4& frame : m_jointFrames)
        drawAxes(frame, std::max(gridStep * 0.35f, m_sceneRadius * 0.035f), 1.0f);
    if (m_tcpPoseVisible)
        drawAxes(m_tcpPose, std::max(gridStep * 0.8f, m_sceneRadius * 0.08f), 3.0f);

    m_program.release();
}

void View3DScene::mousePressEvent(QMouseEvent* event)
{
    m_lastMousePosition = event->position().toPoint();
    event->accept();
}

void View3DScene::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint position = event->position().toPoint();
    const QPoint delta = position - m_lastMousePosition;
    m_lastMousePosition = position;

    if (event->buttons().testFlag(Qt::LeftButton))
    {
        m_yaw -= static_cast<float>(delta.x()) * 0.35f;
        m_pitch = qBound(-88.0f,
            m_pitch - static_cast<float>(delta.y()) * 0.3f, 88.0f);
        update();
    }
    else if (event->buttons().testFlag(Qt::MiddleButton))
    {
        const QVector3D eye = cameraPosition();
        const QVector3D forward = (m_target - eye).normalized();
        const QVector3D right = QVector3D::crossProduct(
            forward, QVector3D(0.0f, 0.0f, 1.0f)).normalized();
        const QVector3D up = QVector3D::crossProduct(right, forward).normalized();
        const float scale = m_distance / static_cast<float>(std::max(1, height()));
        m_target += (-static_cast<float>(delta.x()) * right
                     + static_cast<float>(delta.y()) * up) * scale;
        update();
    }
    event->accept();
}

void View3DScene::wheelEvent(QWheelEvent* event)
{
    const float steps = static_cast<float>(event->angleDelta().y()) / 120.0f;
    m_distance *= std::pow(0.82f, steps);
    m_distance = qBound(
        std::max(0.001f, m_sceneRadius * 0.02f),
        m_distance,
        std::max(1.0f, m_sceneRadius * 200.0f));
    update();
    event->accept();
}

void View3DScene::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        resetCamera();
    event->accept();
}

void View3DScene::updateBounds()
{
    m_hasBounds = false;
    auto includePoint = [this](const QVector3D& point)
    {
        if (!finite(point))
            return;
        if (!m_hasBounds)
        {
            m_boundsMin = point;
            m_boundsMax = point;
            m_hasBounds = true;
            return;
        }
        m_boundsMin.setX(std::min(m_boundsMin.x(), point.x()));
        m_boundsMin.setY(std::min(m_boundsMin.y(), point.y()));
        m_boundsMin.setZ(std::min(m_boundsMin.z(), point.z()));
        m_boundsMax.setX(std::max(m_boundsMax.x(), point.x()));
        m_boundsMax.setY(std::max(m_boundsMax.y(), point.y()));
        m_boundsMax.setZ(std::max(m_boundsMax.z(), point.z()));
    };

    for (const RenderTrajectory& trajectory : m_trajectories)
        for (const QVector3D& point : trajectory.points)
            includePoint(point);
    for (const RenderMesh& mesh : m_meshes)
        for (const MeshVertex& vertex : mesh.geometry.vertices)
            includePoint(mesh.transform.map(vertex.position));
}

void View3DScene::fitBounds()
{
    m_yaw = 45.0f;
    m_pitch = 28.0f;
    if (m_hasBounds)
    {
        m_target = {};
        m_sceneRadius = std::max({
            1.0f, m_boundsMin.length(), m_boundsMax.length()});
    }
    else
    {
        m_target = {};
        m_sceneRadius = 100.0f;
    }
    m_distance = m_sceneRadius * 2.8f;
    m_cameraInitialized = true;
}

void View3DScene::drawLineList(const QVector<LineVertex>& vertices, float width)
{
    if (vertices.isEmpty())
        return;
    m_vertexArray.bind();
    m_vertexBuffer.bind();
    m_vertexBuffer.allocate(vertices.constData(),
        static_cast<int>(vertices.size() * static_cast<qsizetype>(sizeof(LineVertex))));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
        reinterpret_cast<const void*>(offsetof(LineVertex, position)));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
        reinterpret_cast<const void*>(offsetof(LineVertex, color)));
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
        reinterpret_cast<const void*>(offsetof(LineVertex, distance)));
    glLineWidth(width);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(vertices.size()));
    m_vertexBuffer.release();
    m_vertexArray.release();
}

void View3DScene::drawTriangleList(const QVector<LineVertex>& vertices)
{
    if (vertices.isEmpty())
        return;
    m_vertexArray.bind();
    m_vertexBuffer.bind();
    m_vertexBuffer.allocate(vertices.constData(),
        static_cast<int>(vertices.size() * static_cast<qsizetype>(sizeof(LineVertex))));
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
        reinterpret_cast<const void*>(offsetof(LineVertex, position)));
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
        reinterpret_cast<const void*>(offsetof(LineVertex, color)));
    glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
        reinterpret_cast<const void*>(offsetof(LineVertex, distance)));
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
    m_vertexBuffer.release();
    m_vertexArray.release();
}

void View3DScene::drawPolyline(const RenderTrajectory& trajectory)
{
    if (m_trajectoryDisplayMode == TrajectoryDisplayMode::None
        || !trajectory.style.visible || trajectory.points.size() < 2)
        return;

    qsizetype first = 0;
    qsizetype last = trajectory.points.size() - 1;
    if (m_trajectoryDisplayMode == TrajectoryDisplayMode::Local)
    {
        first = m_currentFrame > m_localRadius
            ? m_currentFrame - m_localRadius : 0;
        if (first > last)
            return;
        if (m_currentFrame < last && last - m_currentFrame > m_localRadius)
            last = m_currentFrame + m_localRadius;
        if (last - first < 1)
            return;
    }

    m_program.setUniformValue("linePattern", static_cast<int>(trajectory.style.pattern));
    const QVector3D color = rgb(trajectory.style.color);
    QVector<LineVertex> segment;
    float distance = 0.0f;
    QVector3D previous;
    bool hasPrevious = false;

    auto flush = [this, &segment, &trajectory]()
    {
        if (segment.size() < 2)
        {
            segment.clear();
            return;
        }
        m_vertexArray.bind();
        m_vertexBuffer.bind();
        m_vertexBuffer.allocate(segment.constData(),
            static_cast<int>(segment.size() * static_cast<qsizetype>(sizeof(LineVertex))));
        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
            reinterpret_cast<const void*>(offsetof(LineVertex, position)));
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
            reinterpret_cast<const void*>(offsetof(LineVertex, color)));
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(LineVertex),
            reinterpret_cast<const void*>(offsetof(LineVertex, distance)));
        glLineWidth(trajectory.style.width);
        glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(segment.size()));
        m_vertexBuffer.release();
        m_vertexArray.release();
        segment.clear();
    };

    for (qsizetype index = first; index <= last; ++index)
    {
        const QVector3D& point = trajectory.points.at(index);
        if (!finite(point))
        {
            flush();
            distance = 0.0f;
            hasPrevious = false;
            continue;
        }
        if (hasPrevious)
            distance += (point - previous).length();
        segment.push_back({point, color, distance});
        previous = point;
        hasPrevious = true;
    }
    flush();
}

void View3DScene::drawMesh(const RenderMesh& mesh)
{
    if (mesh.geometry.isEmpty())
        return;
    m_program.setUniformValue("linePattern", 0);
    const QVector3D baseColor = rgb(mesh.color);
    const QVector3D lightDirection = QVector3D(0.35f, -0.45f, 0.82f).normalized();
    QVector<LineVertex> vertices;
    vertices.reserve(mesh.geometry.vertices.size());
    for (const MeshVertex& vertex : mesh.geometry.vertices)
    {
        QVector3D normal = mesh.transform.mapVector(vertex.normal);
        if (!qFuzzyIsNull(normal.lengthSquared()))
            normal.normalize();
        const float light = 0.32f
            + 0.68f * std::max(0.0f, QVector3D::dotProduct(normal, lightDirection));
        vertices.push_back({
            mesh.transform.map(vertex.position), baseColor * light, 0.0f});
    }
    drawTriangleList(vertices);
}

void View3DScene::drawAxes(const QMatrix4x4& transform, float size, float width)
{
    m_program.setUniformValue("linePattern", 0);
    const QVector3D origin = transform.map(QVector3D());
    QVector<LineVertex> vertices;
    vertices.reserve(6);
    vertices.push_back({origin, QVector3D(1.0f, 0.1f, 0.1f), 0.0f});
    vertices.push_back({transform.map(QVector3D(size, 0.0f, 0.0f)),
        QVector3D(1.0f, 0.1f, 0.1f), size});
    vertices.push_back({origin, QVector3D(0.1f, 1.0f, 0.1f), 0.0f});
    vertices.push_back({transform.map(QVector3D(0.0f, size, 0.0f)),
        QVector3D(0.1f, 1.0f, 0.1f), size});
    vertices.push_back({origin, QVector3D(0.15f, 0.35f, 1.0f), 0.0f});
    vertices.push_back({transform.map(QVector3D(0.0f, 0.0f, size)),
        QVector3D(0.15f, 0.35f, 1.0f), size});
    drawLineList(vertices, width);
}

void View3DScene::drawGrid(float extent, float step)
{
    m_program.setUniformValue("linePattern", 0);
    QVector<LineVertex> vertices;
    const QVector3D minor(0.20f, 0.22f, 0.26f);
    const int lineCount = 20;
    vertices.reserve((lineCount * 2 + 2) * 2);
    for (int index = -lineCount / 2; index <= lineCount / 2; ++index)
    {
        const float value = static_cast<float>(index) * step;
        vertices.push_back({QVector3D(-extent, value, 0.0f), minor, 0.0f});
        vertices.push_back({QVector3D(extent, value, 0.0f), minor, extent * 2.0f});
        vertices.push_back({QVector3D(value, -extent, 0.0f), minor, 0.0f});
        vertices.push_back({QVector3D(value, extent, 0.0f), minor, extent * 2.0f});
    }
    drawLineList(vertices, 1.0f);
}

QMatrix4x4 View3DScene::viewProjection() const
{
    QMatrix4x4 projection;
    const float aspect = static_cast<float>(width())
        / static_cast<float>(std::max(1, height()));
    projection.perspective(45.0f, aspect,
        std::max(0.001f, m_distance * 0.001f),
        std::max(10.0f, m_distance + m_sceneRadius * 100.0f));
    QMatrix4x4 view;
    view.lookAt(cameraPosition(), m_target, QVector3D(0.0f, 0.0f, 1.0f));
    return projection * view;
}

QVector3D View3DScene::cameraPosition() const
{
    const float yaw = qDegreesToRadians(m_yaw);
    const float pitch = qDegreesToRadians(m_pitch);
    const QVector3D direction(
        std::cos(pitch) * std::cos(yaw),
        std::cos(pitch) * std::sin(yaw),
        std::sin(pitch));
    return m_target - direction * m_distance;
}

float View3DScene::niceGridStep() const
{
    const float targetStep = std::max(0.001f, m_sceneRadius / 5.0f);
    const float power = std::pow(10.0f, std::floor(std::log10(targetStep)));
    const float fraction = targetStep / power;
    const float nice = fraction < 2.0f ? 1.0f : (fraction < 5.0f ? 2.0f : 5.0f);
    return nice * power;
}

} // namespace view3d
