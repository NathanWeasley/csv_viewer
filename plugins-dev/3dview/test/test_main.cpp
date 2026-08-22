#include "config/view3d_config.h"
#include "model/stl_mesh.h"
#include "playback/playback_state.h"
#include "scene/transform_chain.h"
#include "trajectory/trajectory_data.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDataStream>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QPluginLoader>
#include <QPointer>
#include <QSettings>
#include <QSplitter>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QWidget>
#include <QDoubleSpinBox>

#include <cmath>
#include <iostream>

namespace
{

int failures = 0;

void check(bool condition, const char* expression, int line)
{
    if (condition)
        return;
    std::cerr << "FAILED line " << line << ": " << expression << '\n';
    ++failures;
}

#define CHECK(expression) check((expression), #expression, __LINE__)

bool closeTo(float actual, float expected, float epsilon = 0.0001f)
{
    return std::abs(actual - expected) <= epsilon;
}

class MockSnapshot final : public viewer::plugin::IDataSnapshot
{
public:
    void add(const QString& name, std::initializer_list<double> values)
    {
        m_columns.insert(name, QVector<double>(values));
    }

    quint64 sessionId() const noexcept override { return 7; }
    qsizetype rowCount() const noexcept override
    {
        return m_columns.isEmpty() ? 0 : m_columns.constBegin().value().size();
    }
    QStringList columnNames() const override { return m_columns.keys(); }
    bool contains(const QString& name) const override { return m_columns.contains(name); }
    viewer::plugin::ColumnView column(const QString& name) const override
    {
        const auto it = m_columns.constFind(name);
        if (it == m_columns.constEnd() || it.value().isEmpty())
            return {};
        return {it.value().constData(), it.value().size()};
    }

private:
    QMap<QString, QVector<double>> m_columns;
};

class FakeUiService final : public viewer::plugin::IUiService
{
public:
    viewer::plugin::PluginActionHandle addPluginAction(
        const QString&, const QString&, std::function<void()>) override
    {
        return 1;
    }

    viewer::plugin::PluginMenuHandle addPluginMenu(
        const QString&,
        const QString&,
        const QList<viewer::plugin::PluginMenuItemSpec>&,
        viewer::plugin::PluginMenuCallback callback) override
    {
        menuCallback = std::move(callback);
        return 1;
    }

    bool setPluginMenuItemEnabled(
        viewer::plugin::PluginMenuHandle, const QString&, bool) override
    {
        return true;
    }
    bool setPluginMenuItemChecked(
        viewer::plugin::PluginMenuHandle, const QString&, bool) override
    {
        return true;
    }
    bool setPluginMenuItemVisible(
        viewer::plugin::PluginMenuHandle, const QString&, bool) override
    {
        return true;
    }
    viewer::plugin::PluginDockHandle createDock(
        const QString&,
        const QString&,
        const QString&,
        QWidget*,
        viewer::plugin::DockArea) override
    {
        ++createDockCalls;
        return 1;
    }
    bool showDock(viewer::plugin::PluginDockHandle) override
    {
        ++showDockCalls;
        return true;
    }
    bool closeDock(viewer::plugin::PluginDockHandle) override { return true; }
    void showError(const QString&, const QString& message) override
    {
        lastError = message;
    }
    void showInformation(const QString&, const QString&) override {}

    viewer::plugin::PluginMenuCallback menuCallback;
    QString lastError;
    int createDockCalls = 0;
    int showDockCalls = 0;
};

class FakeHost final : public viewer::plugin::IViewerHost
{
public:
    int sdkVersion() const noexcept override
    {
        return viewer::plugin::kViewerPluginSdkVersion;
    }
    viewer::plugin::IDataService* data() noexcept override { return nullptr; }
    viewer::plugin::IArchiveService* archive() noexcept override { return nullptr; }
    viewer::plugin::IJsonDocumentService* jsonDocuments() noexcept override
    {
        return nullptr;
    }
    viewer::plugin::IEventService* events() noexcept override { return nullptr; }
    viewer::plugin::IPluginRegistry* plugins() noexcept override { return nullptr; }
    viewer::plugin::IUiService* ui() noexcept override { return &uiService; }
    viewer::plugin::ILogService* log() noexcept override { return nullptr; }

    FakeUiService uiService;
};

void testBundledConfig()
{
    const QFileInfo source(QString::fromUtf8(__FILE__));
    const QString path = source.absoluteDir().absoluteFilePath(QStringLiteral("../data/3dview.json"));
    view3d::View3DConfig config;
    QString error;
    CHECK(view3d::ConfigLoader::loadFile(path, &config, &error));
    if (!error.isEmpty())
        std::cerr << error.toStdString() << '\n';
    CHECK(config.jointTracks.value(QStringLiteral("actual_joint")).size() == 3);
    CHECK(config.jointTracks.value(QStringLiteral("actual_joint")).at(2).type
          == view3d::JointType::Prismatic);
    CHECK(config.tcpTracks.value(QStringLiteral("actual_tcp")).size() == 6);
}

void testInvalidJointType()
{
    const QByteArray json = R"json({
      "version": 1,
      "joint": {"j": [{"name": "j1", "type": "hinge"}]},
      "tcp": {"p": ["x", "y", "z", "rx", "ry", "rz"]}
    })json";
    view3d::View3DConfig config;
    QString error;
    CHECK(!view3d::ConfigLoader::parse(json, &config, &error));
    CHECK(error.contains(QStringLiteral("revolute")));
}

void testInvalidTcpShape()
{
    const QByteArray json = R"json({
      "version": 1,
      "joint": {"j": [{"name": "j1", "type": "revolute"}]},
      "tcp": {"p": ["x", "y", "z"]}
    })json";
    view3d::View3DConfig config;
    QString error;
    CHECK(!view3d::ConfigLoader::parse(json, &config, &error));
    CHECK(error.contains(QStringLiteral("six"))
          || error.contains(QStringLiteral("x, y, z")));
}

void testTrajectoryMapping()
{
    auto snapshot = std::make_shared<MockSnapshot>();
    snapshot->add(QStringLiteral("j1"), {10.0, 20.0, 30.0});
    snapshot->add(QStringLiteral("j2"), {1.0, 2.0, 3.0});
    const QStringList tcpColumns = {
        QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z"),
        QStringLiteral("rz"), QStringLiteral("ry"), QStringLiteral("rx")};
    for (qsizetype index = 0; index < tcpColumns.size(); ++index)
        snapshot->add(tcpColumns.at(index),
            {static_cast<double>(index), static_cast<double>(index + 10),
             static_cast<double>(index + 20)});

    view3d::View3DConfig config;
    config.jointTracks.insert(QStringLiteral("actual"), {
        {QStringLiteral("j1"), view3d::JointType::Revolute},
        {QStringLiteral("j2"), view3d::JointType::Prismatic}});
    config.tcpTracks.insert(QStringLiteral("tcp"), tcpColumns);
    config.tcpTracks.insert(QStringLiteral("missing"), {
        QStringLiteral("mx"), QStringLiteral("my"), QStringLiteral("mz"),
        QStringLiteral("ma"), QStringLiteral("mb"), QStringLiteral("mc")});

    view3d::TrajectoryRepository repository;
    QStringList diagnostics;
    CHECK(repository.rebuild(config, snapshot, &diagnostics));
    CHECK(repository.frameCount() == 3);
    CHECK(repository.jointTrack(QStringLiteral("actual")) != nullptr);
    CHECK(repository.tcpTrack(QStringLiteral("tcp")) != nullptr);
    CHECK(repository.tcpTrack(QStringLiteral("missing")) == nullptr);
    CHECK(!diagnostics.isEmpty());
    CHECK(repository.jointValues(QStringLiteral("actual"), 1).at(0) == 20.0);
    CHECK(repository.tcpPosition(QStringLiteral("tcp"), 2)
          == QVector3D(20.0f, 21.0f, 22.0f));
}

void testJointTransformChain()
{
    QVector<view3d::JointVariableConfig> joints = {
        {QStringLiteral("j1"), view3d::JointType::Revolute},
        {QStringLiteral("j2"), view3d::JointType::Prismatic}};
    QVector<view3d::ModelLinkConfig> links(2);
    links[1].translation = QVector3D(10.0f, 0.0f, 0.0f);

    const QVector<QMatrix4x4> transforms = view3d::evaluateJointChain(
        joints, links, {90.0, 5.0}, false);
    CHECK(transforms.size() == 2);
    const QVector3D rotated = transforms.at(0).mapVector(QVector3D(1.0f, 0.0f, 0.0f));
    CHECK(closeTo(rotated.x(), 0.0f));
    CHECK(closeTo(rotated.y(), 1.0f));
    const QVector3D translated = transforms.at(1).map(QVector3D());
    CHECK(closeTo(translated.x(), 0.0f));
    CHECK(closeTo(translated.y(), 10.0f));
    CHECK(closeTo(translated.z(), 5.0f));

    const QMatrix4x4 tcp = view3d::makeTcpPoseTransform(
        QVector3D(1.0f, 2.0f, 3.0f), QVector3D(0.0f, 0.0f, 90.0f), false);
    const QVector3D tcpX = tcp.mapVector(QVector3D(1.0f, 0.0f, 0.0f));
    CHECK(closeTo(tcpX.x(), 0.0f));
    CHECK(closeTo(tcpX.y(), 1.0f));
    CHECK(tcp.map(QVector3D()) == QVector3D(1.0f, 2.0f, 3.0f));

    const QMatrix4x4 ordered = view3d::makeTcpPoseTransform(
        {}, QVector3D(90.0f, 0.0f, 90.0f), false);
    const QVector3D orderedY = ordered.mapVector(QVector3D(0.0f, 1.0f, 0.0f));
    CHECK(closeTo(orderedY.x(), 0.0f));
    CHECK(closeTo(orderedY.y(), 0.0f));
    CHECK(closeTo(orderedY.z(), 1.0f));
}

void testPlaybackState()
{
    view3d::PlaybackState playback;
    playback.setFrameCount(4);
    playback.setSpeed(0.5);
    playback.play(view3d::PlaybackDirection::Forward);
    CHECK(playback.advance() == 0);
    CHECK(playback.advance() == 1);
    playback.setSpeed(2.0);
    CHECK(playback.advance() == 3);
    CHECK(!playback.isPlaying());

    playback.setFrame(3);
    playback.setSpeed(1.5);
    playback.play(view3d::PlaybackDirection::Reverse);
    CHECK(playback.advance() == 2);
    CHECK(playback.advance() == 0);
    CHECK(!playback.isPlaying());
}

void testStlLoader()
{
    const QByteArray ascii = R"stl(solid triangle
facet normal 0 0 1
  outer loop
    vertex 0 0 0
    vertex 1 0 0
    vertex 0 1 0
  endloop
endfacet
endsolid triangle
)stl";
    view3d::StlMesh mesh;
    QString error;
    CHECK(view3d::StlLoader::parse(ascii, &mesh, &error));
    CHECK(mesh.triangleCount() == 1);
    CHECK(mesh.boundsMax == QVector3D(1.0f, 1.0f, 0.0f));

    QByteArray binary(80, '\0');
    QDataStream stream(&binary, QIODevice::Append);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    stream << quint32(1);
    const std::array<float, 12> values = {
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f};
    for (float value : values)
        stream << value;
    stream << quint16(0);
    CHECK(view3d::StlLoader::parse(binary, &mesh, &error));
    CHECK(mesh.triangleCount() == 1);
    CHECK(!view3d::StlLoader::parse(QByteArray("invalid"), &mesh, &error));
}

void testPluginBinary()
{
    const QFileInfo source(QString::fromUtf8(__FILE__));
#ifdef QT_DEBUG
    const QString fileName = QStringLiteral("3dviewd.dll");
#else
    const QString fileName = QStringLiteral("3dview.dll");
#endif
    const QString path = source.absoluteDir().absoluteFilePath(
        QStringLiteral("../lib/") + fileName);
    QPluginLoader loader(path);
    QObject* instance = loader.instance();
    CHECK(instance != nullptr);
    if (!instance)
    {
        std::cerr << loader.errorString().toStdString() << '\n';
        return;
    }
    auto* plugin = qobject_cast<viewer::plugin::IViewerPlugin*>(instance);
    CHECK(plugin != nullptr);
    if (plugin)
    {
        CHECK(plugin->id() == QStringLiteral("3dview"));

        QTemporaryDir runtimeDirectory(
            source.absoluteDir().absoluteFilePath(QStringLiteral("runtime-XXXXXX")));
        CHECK(runtimeDirectory.isValid());
        CHECK(QDir().mkpath(runtimeDirectory.filePath(QStringLiteral("data"))));
        CHECK(QFile::copy(
            source.absoluteDir().absoluteFilePath(QStringLiteral("../data/3dview.json")),
            runtimeDirectory.filePath(QStringLiteral("data/3dview.json"))));
        instance->setProperty(
            viewer::plugin::kPluginRootDirectoryProperty,
            runtimeDirectory.path());
        FakeHost host;
        CHECK(plugin->initialize(&host));
        CHECK(static_cast<bool>(host.uiService.menuCallback));
        if (host.uiService.menuCallback)
        {
            host.uiService.menuCallback(QStringLiteral("show"), false);
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            QWidget* view = nullptr;
            for (QWidget* candidate : QApplication::topLevelWidgets())
            {
                if (candidate->objectName() == QStringLiteral("3dview.content"))
                {
                    view = candidate;
                    break;
                }
            }
            CHECK(host.uiService.createDockCalls == 0);
            CHECK(host.uiService.showDockCalls == 0);
            CHECK(view != nullptr);
            if (view)
            {
                QPointer<QWidget> guard(view);
                CHECK(view->isWindow());
                CHECK(view->parentWidget() == nullptr);
                CHECK(!view->testAttribute(Qt::WA_QuitOnClose));
                CHECK(view->isVisible());
                view->resize(913, 617);
                if (auto* splitter = view->findChild<QSplitter*>(
                        QStringLiteral("3dview.mainSplitter")))
                    splitter->setSizes({650, 263});
                else
                    CHECK(false);
                if (auto* speed = view->findChild<QDoubleSpinBox*>(
                        QStringLiteral("3dview.playbackSpeed")))
                    speed->setValue(2.5);
                else
                    CHECK(false);
                if (auto* mode = view->findChild<QComboBox*>(
                        QStringLiteral("3dview.trajectoryMode")))
                {
                    const int localIndex = mode->findData(2);
                    CHECK(localIndex >= 0);
                    mode->setCurrentIndex(localIndex);
                }
                else
                    CHECK(false);
                if (auto* localSamples = view->findChild<QSpinBox*>(
                        QStringLiteral("3dview.localSamples")))
                    localSamples->setValue(42);
                else
                    CHECK(false);
                view->close();
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                CHECK(!guard.isNull());
                CHECK(!view->isVisible());
                const QString statePath = runtimeDirectory.filePath(
                    QStringLiteral("3dview.ini"));
                CHECK(QFileInfo::exists(statePath));
                QSettings state(statePath, QSettings::IniFormat);
                CHECK(!state.value(QStringLiteral("window/geometry")).toByteArray().isEmpty());
                CHECK(!state.value(QStringLiteral("layout/mainSplitter")).toByteArray().isEmpty());
                CHECK(closeTo(
                    static_cast<float>(state.value(QStringLiteral("controls/speed")).toDouble()),
                    2.5f));
                CHECK(state.value(QStringLiteral("controls/trajectoryMode")).toInt() == 2);
                CHECK(state.value(QStringLiteral("controls/localSamples")).toInt() == 42);
                host.uiService.menuCallback(QStringLiteral("show"), false);
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
                CHECK(!guard.isNull());
                CHECK(view->isVisible());
                plugin->shutdown();
                CHECK(guard.isNull());
            }
            else
            {
                plugin->shutdown();
            }
        }
        host.uiService.menuCallback = {};
    }
    CHECK(loader.unload());
}

} // namespace

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    qputenv("QT_OPENGL", QByteArrayLiteral("software"));
    QApplication application(argc, argv);
    testBundledConfig();
    testInvalidJointType();
    testInvalidTcpShape();
    testTrajectoryMapping();
    testJointTransformChain();
    testPlaybackState();
    testStlLoader();
    testPluginBinary();
    if (failures == 0)
        std::cout << "All 3dview tests passed.\n";
    return failures == 0 ? 0 : 1;
}
