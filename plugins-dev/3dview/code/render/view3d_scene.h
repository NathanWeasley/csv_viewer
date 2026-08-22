#pragma once

#include "model/stl_mesh.h"

#include <QColor>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector>
#include <QVector3D>

namespace view3d
{

enum class LinePattern
{
    Solid = 0,
    Dashed = 1,
    Dotted = 2
};

enum class TrajectoryDisplayMode
{
    None = 0,
    All = 1,
    Local = 2
};

struct TrajectoryStyle
{
    bool visible = true;
    QColor color = QColor(35, 145, 255);
    float width = 2.0f;
    LinePattern pattern = LinePattern::Solid;
};

struct RenderTrajectory
{
    QString name;
    QVector<QVector3D> points;
    TrajectoryStyle style;
};

struct RenderMesh
{
    QString name;
    StlMesh geometry;
    QColor color = QColor(180, 185, 195);
    QMatrix4x4 transform;
    int linkIndex = -1;
};

class View3DScene final
    : public QOpenGLWidget
    , protected QOpenGLFunctions_3_3_Core
{
public:
    explicit View3DScene(QWidget* parent = nullptr);
    ~View3DScene() override;

    void setTrajectories(const QVector<RenderTrajectory>& trajectories);
    void setTrajectoryStyle(const QString& name, const TrajectoryStyle& style);
    void setTrajectoryDisplay(
        TrajectoryDisplayMode mode,
        qsizetype currentFrame,
        qsizetype localRadius);
    void setMeshes(const QVector<RenderMesh>& meshes);
    void setMeshTransforms(const QVector<QMatrix4x4>& linkTransforms);
    void setTcpPose(const QMatrix4x4& pose, bool visible);
    void setJointFrames(const QVector<QMatrix4x4>& frames);
    void resetCamera();

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    struct LineVertex
    {
        QVector3D position;
        QVector3D color;
        float distance = 0.0f;
    };

    void updateBounds();
    void fitBounds();
    void drawLineList(const QVector<LineVertex>& vertices, float width);
    void drawTriangleList(const QVector<LineVertex>& vertices);
    void drawPolyline(const RenderTrajectory& trajectory);
    void drawMesh(const RenderMesh& mesh);
    void drawAxes(const QMatrix4x4& transform, float size, float width);
    void drawGrid(float extent, float step);
    QMatrix4x4 viewProjection() const;
    QVector3D cameraPosition() const;
    float niceGridStep() const;

    QVector<RenderTrajectory> m_trajectories;
    TrajectoryDisplayMode m_trajectoryDisplayMode = TrajectoryDisplayMode::All;
    qsizetype m_currentFrame = 0;
    qsizetype m_localRadius = 100;
    QVector<RenderMesh> m_meshes;
    QVector<QMatrix4x4> m_jointFrames;
    QMatrix4x4 m_tcpPose;
    bool m_tcpPoseVisible = false;
    QOpenGLShaderProgram m_program;
    QOpenGLBuffer m_vertexBuffer{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject m_vertexArray;
    bool m_glReady = false;
    bool m_hasBounds = false;
    bool m_cameraInitialized = false;
    QVector3D m_boundsMin;
    QVector3D m_boundsMax;
    QVector3D m_target;
    float m_sceneRadius = 100.0f;
    float m_distance = 350.0f;
    float m_yaw = 45.0f;
    float m_pitch = 28.0f;
    QPoint m_lastMousePosition;
};

} // namespace view3d
