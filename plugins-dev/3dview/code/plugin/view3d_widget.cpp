#include "view3d_widget.h"

#include "model/stl_mesh.h"
#include "scene/transform_chain.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <array>
#include <limits>

namespace
{

QString styleGroup(const QString& trackName)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(trackName));
}

void updateColorButton(QPushButton* button, const QColor& color)
{
    button->setText(QStringLiteral(" "));
    button->setToolTip(color.name(QColor::HexRgb));
    button->setStyleSheet(QStringLiteral("background-color: %1;").arg(color.name()));
}

} // namespace

View3DWidget::View3DWidget(
    const QString& configPath,
    const QString& statePath,
    QWidget* parent)
    : QWidget(parent)
    , m_configPath(configPath)
    , m_statePath(statePath)
{
    setWindowFlag(Qt::Window, true);
    setAttribute(Qt::WA_QuitOnClose, false);
    setObjectName(QStringLiteral("3dview.content"));
    setMinimumSize(760, 480);
    setupUi();
    resize(1180, 760);
    restoreUiState();
    reloadConfiguration();
}

View3DWidget::~View3DWidget()
{
    saveUiState();
}

void View3DWidget::closeEvent(QCloseEvent* event)
{
    pausePlayback();
    saveUiState();
    QWidget::closeEvent(event);
}

void View3DWidget::setupUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("3dview.mainSplitter"));
    m_viewport = new view3d::View3DScene(m_splitter);
    m_viewport->setObjectName(QStringLiteral("3dview.viewport"));

    auto* sidebar = new QWidget(m_splitter);
    sidebar->setMinimumWidth(280);
    sidebar->setMaximumWidth(420);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    auto* form = new QFormLayout;
    m_presetCombo = new QComboBox(sidebar);
    m_jointCombo = new QComboBox(sidebar);
    m_poseCombo = new QComboBox(sidebar);
    form->addRow(QString::fromUtf8(u8"组合："), m_presetCombo);
    form->addRow(QString::fromUtf8(u8"模型跟随："), m_jointCombo);
    form->addRow(QString::fromUtf8(u8"TCP 姿态："), m_poseCombo);
    sidebarLayout->addLayout(form);

    auto* trackTitle = new QLabel(QString::fromUtf8(u8"TCP 轨迹"), sidebar);
    sidebarLayout->addWidget(trackTitle);
    m_trackTable = new QTableWidget(sidebar);
    m_trackTable->setColumnCount(5);
    m_trackTable->setHorizontalHeaderLabels({
        QString::fromUtf8(u8"显示"), QString::fromUtf8(u8"名称"),
        QString::fromUtf8(u8"颜色"), QString::fromUtf8(u8"线型"),
        QString::fromUtf8(u8"线宽")});
    m_trackTable->verticalHeader()->hide();
    m_trackTable->horizontalHeader()->setObjectName(
        QStringLiteral("3dview.trackHeader"));
    m_trackTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_trackTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_trackTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_trackTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_trackTable->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_trackTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_trackTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    sidebarLayout->addWidget(m_trackTable, 1);

    auto* resetCamera = new QPushButton(QString::fromUtf8(u8"复位视角"), sidebar);
    sidebarLayout->addWidget(resetCamera);
    auto* help = new QLabel(
        QString::fromUtf8(u8"左键拖动旋转；中键拖动平移；滚轮缩放；双击复位。"),
        sidebar);
    help->setWordWrap(true);
    sidebarLayout->addWidget(help);

    m_splitter->addWidget(m_viewport);
    m_splitter->addWidget(sidebar);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setSizes({850, 330});
    rootLayout->addWidget(m_splitter, 1);

    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("3dview.status"));
    m_status->setContentsMargins(8, 4, 8, 4);
    m_status->setWordWrap(true);
    rootLayout->addWidget(m_status);

    auto* player = new QWidget(this);
    auto* playerLayout = new QHBoxLayout(player);
    playerLayout->setContentsMargins(6, 4, 6, 4);
    m_reverseButton = new QToolButton(player);
    m_previousButton = new QToolButton(player);
    m_pauseButton = new QToolButton(player);
    m_nextButton = new QToolButton(player);
    m_forwardButton = new QToolButton(player);
    m_reverseButton->setText(QString::fromUtf8(u8"反向 ◀"));
    m_previousButton->setText(QString::fromUtf8(u8"上一帧"));
    m_pauseButton->setText(QString::fromUtf8(u8"暂停"));
    m_nextButton->setText(QString::fromUtf8(u8"下一帧"));
    m_forwardButton->setText(QString::fromUtf8(u8"▶ 正向"));
    m_speedSpin = new QDoubleSpinBox(player);
    m_speedSpin->setObjectName(QStringLiteral("3dview.playbackSpeed"));
    m_speedSpin->setRange(0.05, 100.0);
    m_speedSpin->setDecimals(2);
    m_speedSpin->setSingleStep(0.25);
    m_speedSpin->setValue(1.0);
    m_speedSpin->setSuffix(QString::fromUtf8(u8" 倍"));
    m_frameSlider = new QSlider(Qt::Horizontal, player);
    m_frameSlider->setObjectName(QStringLiteral("3dview.frameSlider"));
    m_frameLabel = new QLabel(QStringLiteral("0 / 0"), player);
    playerLayout->addWidget(m_reverseButton);
    playerLayout->addWidget(m_previousButton);
    playerLayout->addWidget(m_pauseButton);
    playerLayout->addWidget(m_nextButton);
    playerLayout->addWidget(m_forwardButton);
    playerLayout->addWidget(m_speedSpin);
    playerLayout->addWidget(m_frameSlider, 1);
    playerLayout->addWidget(m_frameLabel);
    rootLayout->addWidget(player);

    m_timer = new QTimer(this);
    m_timer->setInterval(16);
    m_timer->setTimerType(Qt::PreciseTimer);

    connect(resetCamera, &QPushButton::clicked,
        m_viewport, &view3d::View3DScene::resetCamera);
    connect(m_presetCombo, &QComboBox::currentTextChanged, this,
        [this](const QString& name)
        {
            if (!m_rebuildingControls)
                applyPreset(name);
        });
    connect(m_jointCombo, &QComboBox::currentTextChanged, this,
        [this](const QString& name)
        {
            if (m_rebuildingControls)
                return;
            m_activeJointTrack = name;
            m_savedJointTrack = name;
            updateFrame(m_playback.frame());
        });
    connect(m_poseCombo, &QComboBox::currentTextChanged, this,
        [this](const QString& name)
        {
            if (m_rebuildingControls)
                return;
            m_activePoseTrack = name;
            m_savedPoseTrack = name;
            updateFrame(m_playback.frame());
        });
    connect(m_reverseButton, &QToolButton::clicked, this,
        [this] { startPlayback(view3d::PlaybackDirection::Reverse); });
    connect(m_forwardButton, &QToolButton::clicked, this,
        [this] { startPlayback(view3d::PlaybackDirection::Forward); });
    connect(m_pauseButton, &QToolButton::clicked, this,
        [this] { pausePlayback(); });
    connect(m_previousButton, &QToolButton::clicked, this,
        [this]
        {
            pausePlayback();
            setFrame(m_playback.frame() - 1);
        });
    connect(m_nextButton, &QToolButton::clicked, this,
        [this]
        {
            pausePlayback();
            setFrame(m_playback.frame() + 1);
        });
    connect(m_speedSpin, &QDoubleSpinBox::valueChanged, this,
        [this](double speed) { m_playback.setSpeed(speed); });
    connect(m_frameSlider, &QSlider::sliderPressed, this,
        [this] { pausePlayback(); });
    connect(m_frameSlider, &QSlider::valueChanged, this,
        [this](int frame)
        {
            if (!m_rebuildingControls)
                setFrame(frame);
        });
    connect(m_timer, &QTimer::timeout, this, [this] { playbackTick(); });
}

void View3DWidget::restoreUiState()
{
    if (m_statePath.isEmpty() || !QFileInfo::exists(m_statePath))
        return;

    QSettings settings(m_statePath, QSettings::IniFormat);
    const QByteArray geometry = settings.value(QStringLiteral("window/geometry")).toByteArray();
    if (!geometry.isEmpty())
        restoreGeometry(geometry);
    const QByteArray splitterState =
        settings.value(QStringLiteral("layout/mainSplitter")).toByteArray();
    if (!splitterState.isEmpty())
        m_splitter->restoreState(splitterState);
    const QByteArray headerState =
        settings.value(QStringLiteral("layout/trackHeader")).toByteArray();
    if (!headerState.isEmpty())
        m_trackTable->horizontalHeader()->restoreState(headerState);

    m_speedSpin->setValue(
        settings.value(QStringLiteral("controls/speed"), 1.0).toDouble());
    m_savedPreset = settings.value(QStringLiteral("controls/preset")).toString();
    m_savedJointTrack = settings.value(QStringLiteral("controls/jointTrack")).toString();
    m_savedPoseTrack = settings.value(QStringLiteral("controls/poseTrack")).toString();
    m_savedFrame = std::max<qint64>(
        0, settings.value(QStringLiteral("controls/frame"), 0).toLongLong());
    m_controlStateLoaded = settings.value(
        QStringLiteral("controls/valid"), false).toBool()
        && !m_savedPreset.isEmpty();
}

void View3DWidget::saveUiState() const
{
    if (m_statePath.isEmpty() || !m_splitter || !m_trackTable)
        return;

    QSettings settings(m_statePath, QSettings::IniFormat);
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("layout/mainSplitter"), m_splitter->saveState());
    settings.setValue(QStringLiteral("layout/trackHeader"),
        m_trackTable->horizontalHeader()->saveState());
    settings.setValue(QStringLiteral("controls/speed"), m_speedSpin->value());
    settings.setValue(QStringLiteral("controls/preset"),
        m_presetCombo->currentText().isEmpty()
            ? m_savedPreset : m_presetCombo->currentText());
    settings.setValue(QStringLiteral("controls/jointTrack"),
        m_jointCombo->currentText().isEmpty()
            ? m_savedJointTrack : m_jointCombo->currentText());
    settings.setValue(QStringLiteral("controls/poseTrack"),
        m_poseCombo->currentText().isEmpty()
            ? m_savedPoseTrack : m_poseCombo->currentText());
    settings.setValue(QStringLiteral("controls/frame"),
        m_playback.frameCount() > 0 ? m_playback.frame() : m_savedFrame);
    settings.setValue(QStringLiteral("controls/valid"),
        m_controlsInitialized && !m_presetCombo->currentText().isEmpty());
    settings.sync();
    for (auto it = m_styles.constBegin(); it != m_styles.constEnd(); ++it)
        saveStyle(it.key());
}

bool View3DWidget::reloadConfiguration()
{
    view3d::View3DConfig parsed;
    QString error;
    if (!view3d::ConfigLoader::loadFile(m_configPath, &parsed, &error))
    {
        m_lastError = error;
        m_status->setText(QString::fromUtf8(u8"配置错误：") + error);
        return false;
    }

    pausePlayback();
    m_config = std::move(parsed);
    m_configLoaded = true;
    m_lastError.clear();
    loadModels();
    rebuildRepository();
    return true;
}

void View3DWidget::setDataSnapshot(viewer::plugin::DataSnapshotPtr snapshot)
{
    pausePlayback();
    m_snapshot = std::move(snapshot);
    rebuildRepository();
}

void View3DWidget::clearData()
{
    pausePlayback();
    if (!m_presetCombo->currentText().isEmpty())
        m_savedPreset = m_presetCombo->currentText();
    if (!m_jointCombo->currentText().isEmpty())
        m_savedJointTrack = m_jointCombo->currentText();
    if (!m_poseCombo->currentText().isEmpty())
        m_savedPoseTrack = m_poseCombo->currentText();
    if (m_playback.frameCount() > 0)
        m_savedFrame = m_playback.frame();
    m_snapshot.reset();
    m_repository.clear();
    m_playback.setTimeline({});
    m_styles.clear();
    m_viewport->setTrajectories({});
    m_viewport->setJointFrames({});
    m_viewport->setTcpPose({}, false);
    m_rebuildingControls = true;
    m_presetCombo->clear();
    m_jointCombo->clear();
    m_poseCombo->clear();
    m_trackTable->setRowCount(0);
    m_frameSlider->setRange(0, 0);
    m_frameLabel->setText(QStringLiteral("0 / 0"));
    m_rebuildingControls = false;
    m_status->setText(QString::fromUtf8(u8"尚未载入 Viewer 数据。"));
}

QString View3DWidget::lastError() const
{
    return m_lastError;
}

void View3DWidget::rebuildRepository()
{
    if (!m_configLoaded)
        return;
    if (!m_snapshot)
    {
        clearData();
        return;
    }

    QStringList diagnostics;
    m_repository.rebuild(m_config, m_snapshot, &diagnostics);
    rebuildScene();
    rebuildControls();
    const QString summary = QString::fromUtf8(u8"已映射 %1 组关节轨迹、%2 组 TCP 轨迹，共 %3 帧。")
                                .arg(m_repository.jointTracks().size())
                                .arg(m_repository.tcpTracks().size())
                                .arg(m_repository.frameCount());
    diagnostics.append(m_modelDiagnostics);
    m_status->setText(diagnostics.isEmpty()
        ? summary
        : summary + QStringLiteral("\n") + diagnostics.join(QStringLiteral("\n")));
}

void View3DWidget::rebuildScene()
{
    static const std::array<QColor, 8> colors = {
        QColor(39, 154, 241), QColor(255, 174, 50),
        QColor(168, 102, 255), QColor(37, 194, 125),
        QColor(242, 91, 117), QColor(75, 210, 224),
        QColor(238, 219, 75), QColor(196, 128, 85)};

    QVector<view3d::RenderTrajectory> rendered;
    QMap<QString, view3d::TrajectoryStyle> currentStyles;
    int colorIndex = 0;
    for (auto it = m_repository.tcpTracks().constBegin();
         it != m_repository.tcpTracks().constEnd(); ++it, ++colorIndex)
    {
        view3d::RenderTrajectory trajectory;
        trajectory.name = it.key();
        view3d::TrajectoryStyle fallback;
        fallback.color = colors[static_cast<size_t>(colorIndex) % colors.size()];
        trajectory.style = m_styles.contains(it.key())
            ? m_styles.value(it.key()) : loadStyle(it.key(), fallback);
        currentStyles.insert(it.key(), trajectory.style);
        trajectory.points.reserve(it.value().sampleCount);
        for (qsizetype frame = 0; frame < it.value().sampleCount; ++frame)
        {
            trajectory.points.push_back(QVector3D(
                static_cast<float>(it.value().values[0].data[frame]),
                static_cast<float>(it.value().values[1].data[frame]),
                static_cast<float>(it.value().values[2].data[frame])));
        }
        rendered.push_back(std::move(trajectory));
    }
    m_styles = currentStyles;
    m_viewport->setTrajectories(rendered);
    m_viewport->resetCamera();
}

void View3DWidget::loadModels()
{
    m_modelDiagnostics.clear();
    QVector<view3d::RenderMesh> meshes;
    if (m_config.model.isEmpty())
    {
        m_viewport->setMeshes(meshes);
        return;
    }

    const QDir configDirectory(QFileInfo(m_configPath).absolutePath());
    const QString configuredDirectory = m_config.model.directory.trimmed();
    const QString modelPath = configuredDirectory.isEmpty()
        ? configDirectory.absolutePath()
        : (QDir::isAbsolutePath(configuredDirectory)
              ? QDir(configuredDirectory).absolutePath()
              : configDirectory.absoluteFilePath(configuredDirectory));
    const QDir modelDirectory(modelPath);
    if (!modelDirectory.exists())
    {
        m_modelDiagnostics.push_back(
            QString::fromUtf8(u8"STL 目录不存在：") + modelPath);
        m_viewport->setMeshes(meshes);
        return;
    }

    static const std::array<QColor, 6> colors = {
        QColor(175, 180, 190), QColor(110, 150, 205),
        QColor(195, 135, 95), QColor(115, 175, 135),
        QColor(175, 125, 190), QColor(190, 175, 105)};
    auto addMesh = [&](const QString& fileName, int linkIndex, const QColor& color)
    {
        if (fileName.trimmed().isEmpty())
            return;
        view3d::RenderMesh rendered;
        rendered.name = fileName;
        rendered.linkIndex = linkIndex;
        rendered.color = color;
        QString error;
        const QString path = modelDirectory.absoluteFilePath(fileName);
        if (!view3d::StlLoader::loadFile(path, &rendered.geometry, &error))
        {
            m_modelDiagnostics.push_back(error);
            return;
        }
        meshes.push_back(std::move(rendered));
    };

    addMesh(m_config.model.baseStl, -1, colors[0]);
    for (qsizetype index = 0; index < m_config.model.links.size(); ++index)
    {
        addMesh(m_config.model.links.at(index).stlFile,
            static_cast<int>(index),
            colors[static_cast<size_t>(index + 1) % colors.size()]);
    }
    if (m_config.model.baseStl.isEmpty() && m_config.model.links.isEmpty())
    {
        const QStringList files = modelDirectory.entryList(
            {QStringLiteral("*.stl"), QStringLiteral("*.STL")},
            QDir::Files, QDir::Name | QDir::IgnoreCase);
        for (qsizetype index = 0; index < files.size(); ++index)
            addMesh(files.at(index), -1, colors[static_cast<size_t>(index) % colors.size()]);
    }

    if (meshes.isEmpty())
        m_modelDiagnostics.push_back(
            QString::fromUtf8(u8"STL 目录中没有成功载入的模型：") + modelPath);
    m_viewport->setMeshes(meshes);

    QVector<view3d::JointVariableConfig> variables;
    const auto preset = m_config.presets.constFind(m_config.defaultPreset);
    if (preset != m_config.presets.constEnd())
        variables = m_config.jointTracks.value(preset.value().jointTrack);
    if (variables.isEmpty() && !m_config.jointTracks.isEmpty())
        variables = m_config.jointTracks.constBegin().value();
    QVector<double> zeroValues(variables.size(), 0.0);
    const QVector<QMatrix4x4> transforms = view3d::evaluateJointChain(
        variables, m_config.model.links, zeroValues,
        m_config.angleUnit == QStringLiteral("rad"));
    m_viewport->setMeshTransforms(transforms);
    m_viewport->resetCamera();
}

void View3DWidget::rebuildControls()
{
    const QString desiredPreset = !m_presetCombo->currentText().isEmpty()
        ? m_presetCombo->currentText() : m_savedPreset;
    const QString desiredJoint = !m_jointCombo->currentText().isEmpty()
        ? m_jointCombo->currentText() : m_savedJointTrack;
    const QString desiredPose = !m_poseCombo->currentText().isEmpty()
        ? m_poseCombo->currentText() : m_savedPoseTrack;
    const qsizetype desiredFrame = m_playback.frameCount() > 0
        ? m_playback.frame() : m_savedFrame;
    const bool restoreControls = m_controlsInitialized || m_controlStateLoaded;

    m_rebuildingControls = true;
    m_presetCombo->clear();
    m_presetCombo->addItems(m_config.presets.keys());
    m_jointCombo->clear();
    m_jointCombo->addItems(m_repository.jointTracks().keys());
    m_poseCombo->clear();
    m_poseCombo->addItems(m_repository.tcpTracks().keys());
    rebuildTrackTable();

    QVector<double> timeline;
    timeline.reserve(m_repository.frameCount());
    for (qsizetype frame = 0; frame < m_repository.frameCount(); ++frame)
        timeline.push_back(m_repository.timeAt(frame));
    m_playback.setTimeline(timeline);
    m_playback.setSpeed(m_speedSpin->value());
    const qsizetype maximum = std::min<qsizetype>(
        std::max<qsizetype>(0, m_repository.frameCount() - 1),
        std::numeric_limits<int>::max());
    m_frameSlider->setRange(0, static_cast<int>(maximum));

    int presetIndex = m_presetCombo->findText(desiredPreset);
    if (presetIndex < 0)
        presetIndex = m_presetCombo->findText(m_config.defaultPreset);
    if (presetIndex < 0 && m_presetCombo->count() > 0)
        presetIndex = 0;
    m_presetCombo->setCurrentIndex(presetIndex);

    if (restoreControls && presetIndex >= 0)
    {
        const view3d::PresetConfig preset =
            m_config.presets.value(m_presetCombo->currentText());
        int jointIndex = m_jointCombo->findText(desiredJoint);
        if (jointIndex < 0)
            jointIndex = m_jointCombo->findText(preset.jointTrack);
        if (jointIndex < 0 && m_jointCombo->count() > 0)
            jointIndex = 0;
        m_jointCombo->setCurrentIndex(jointIndex);

        int poseIndex = m_poseCombo->findText(desiredPose);
        if (poseIndex < 0)
            poseIndex = m_poseCombo->findText(preset.poseTrack);
        if (poseIndex < 0 && m_poseCombo->count() > 0)
            poseIndex = 0;
        m_poseCombo->setCurrentIndex(poseIndex);
        m_activeJointTrack = m_jointCombo->currentText();
        m_activePoseTrack = m_poseCombo->currentText();
    }
    m_rebuildingControls = false;

    if (!restoreControls && presetIndex >= 0)
        applyPreset(m_presetCombo->currentText());
    else
    {
        applyTrajectoryStyles();
        setFrame(desiredFrame);
    }
    m_controlsInitialized = true;
}

void View3DWidget::rebuildTrackTable()
{
    m_trackTable->setRowCount(m_repository.tcpTracks().size());
    int row = 0;
    for (auto it = m_repository.tcpTracks().constBegin();
         it != m_repository.tcpTracks().constEnd(); ++it, ++row)
    {
        const QString name = it.key();
        view3d::TrajectoryStyle& style = m_styles[name];

        auto* visible = new QCheckBox(m_trackTable);
        visible->setChecked(style.visible);
        visible->setToolTip(QString::fromUtf8(u8"显示或隐藏该轨迹"));
        m_trackTable->setCellWidget(row, 0, visible);
        m_trackTable->setItem(row, 1, new QTableWidgetItem(name));

        auto* color = new QPushButton(m_trackTable);
        color->setFixedWidth(30);
        updateColorButton(color, style.color);
        m_trackTable->setCellWidget(row, 2, color);

        auto* pattern = new QComboBox(m_trackTable);
        pattern->addItem(QString::fromUtf8(u8"实线"), static_cast<int>(view3d::LinePattern::Solid));
        pattern->addItem(QString::fromUtf8(u8"虚线"), static_cast<int>(view3d::LinePattern::Dashed));
        pattern->addItem(QString::fromUtf8(u8"点线"), static_cast<int>(view3d::LinePattern::Dotted));
        pattern->setCurrentIndex(pattern->findData(static_cast<int>(style.pattern)));
        m_trackTable->setCellWidget(row, 3, pattern);

        auto* width = new QDoubleSpinBox(m_trackTable);
        width->setRange(1.0, 10.0);
        width->setDecimals(1);
        width->setSingleStep(0.5);
        width->setValue(style.width);
        m_trackTable->setCellWidget(row, 4, width);

        connect(visible, &QCheckBox::toggled, this,
            [this, name](bool checked)
            {
                if (m_rebuildingControls)
                    return;
                m_styles[name].visible = checked;
                saveStyle(name);
                applyTrajectoryStyles();
            });
        connect(color, &QPushButton::clicked, this,
            [this, name, color]
            {
                const QColor selected = QColorDialog::getColor(
                    m_styles.value(name).color, this,
                    QString::fromUtf8(u8"选择轨迹颜色"));
                if (!selected.isValid())
                    return;
                m_styles[name].color = selected;
                updateColorButton(color, selected);
                saveStyle(name);
                applyTrajectoryStyles();
            });
        connect(pattern, &QComboBox::currentIndexChanged, this,
            [this, name, pattern](int)
            {
                if (m_rebuildingControls)
                    return;
                m_styles[name].pattern = static_cast<view3d::LinePattern>(
                    pattern->currentData().toInt());
                saveStyle(name);
                applyTrajectoryStyles();
            });
        connect(width, &QDoubleSpinBox::valueChanged, this,
            [this, name](double value)
            {
                if (m_rebuildingControls)
                    return;
                m_styles[name].width = static_cast<float>(value);
                saveStyle(name);
                applyTrajectoryStyles();
            });
    }
}

void View3DWidget::applyPreset(const QString& presetName)
{
    const auto presetIt = m_config.presets.constFind(presetName);
    if (presetIt == m_config.presets.constEnd())
        return;

    m_rebuildingControls = true;
    m_savedPreset = presetName;
    const view3d::PresetConfig& preset = presetIt.value();
    const int jointIndex = m_jointCombo->findText(preset.jointTrack);
    if (jointIndex >= 0)
        m_jointCombo->setCurrentIndex(jointIndex);
    const int poseIndex = m_poseCombo->findText(preset.poseTrack);
    if (poseIndex >= 0)
        m_poseCombo->setCurrentIndex(poseIndex);
    m_activeJointTrack = m_jointCombo->currentText();
    m_activePoseTrack = m_poseCombo->currentText();
    m_savedJointTrack = m_activeJointTrack;
    m_savedPoseTrack = m_activePoseTrack;

    for (int row = 0; row < m_trackTable->rowCount(); ++row)
    {
        const QString name = m_trackTable->item(row, 1)->text();
        const bool visible = preset.tcpTracks.contains(name);
        m_styles[name].visible = visible;
        if (auto* checkbox = qobject_cast<QCheckBox*>(m_trackTable->cellWidget(row, 0)))
            checkbox->setChecked(visible);
    }
    m_rebuildingControls = false;
    applyTrajectoryStyles();
    setFrame(0);
}

void View3DWidget::applyTrajectoryStyles()
{
    for (auto it = m_styles.constBegin(); it != m_styles.constEnd(); ++it)
        m_viewport->setTrajectoryStyle(it.key(), it.value());
}

view3d::TrajectoryStyle View3DWidget::loadStyle(
    const QString& trackName,
    const view3d::TrajectoryStyle& fallback) const
{
    QSettings settings(m_statePath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("plugins/3dview/trajectoryStyles"));
    settings.beginGroup(styleGroup(trackName));
    view3d::TrajectoryStyle style = fallback;
    const bool hasPluginStyle = settings.contains(QStringLiteral("visible"))
        || settings.contains(QStringLiteral("color"))
        || settings.contains(QStringLiteral("width"))
        || settings.contains(QStringLiteral("pattern"));
    if (hasPluginStyle)
    {
        style.visible = settings.value(QStringLiteral("visible"), style.visible).toBool();
        style.color = settings.value(QStringLiteral("color"), style.color).value<QColor>();
        style.width = settings.value(QStringLiteral("width"), style.width).toFloat();
        style.pattern = static_cast<view3d::LinePattern>(
            settings.value(QStringLiteral("pattern"), static_cast<int>(style.pattern)).toInt());
        return style;
    }

    // One-time, read-only migration from the previous application-wide store.
    QSettings legacy;
    legacy.beginGroup(QStringLiteral("plugins/3dview/trajectoryStyles"));
    legacy.beginGroup(styleGroup(trackName));
    const bool hasLegacyStyle = legacy.contains(QStringLiteral("visible"))
        || legacy.contains(QStringLiteral("color"))
        || legacy.contains(QStringLiteral("width"))
        || legacy.contains(QStringLiteral("pattern"));
    if (hasLegacyStyle)
    {
        style.visible = legacy.value(QStringLiteral("visible"), style.visible).toBool();
        style.color = legacy.value(QStringLiteral("color"), style.color).value<QColor>();
        style.width = legacy.value(QStringLiteral("width"), style.width).toFloat();
        style.pattern = static_cast<view3d::LinePattern>(
            legacy.value(QStringLiteral("pattern"), static_cast<int>(style.pattern)).toInt());
        settings.setValue(QStringLiteral("visible"), style.visible);
        settings.setValue(QStringLiteral("color"), style.color);
        settings.setValue(QStringLiteral("width"), style.width);
        settings.setValue(QStringLiteral("pattern"), static_cast<int>(style.pattern));
        settings.sync();
    }
    return style;
}

void View3DWidget::saveStyle(const QString& trackName) const
{
    const auto it = m_styles.constFind(trackName);
    if (it == m_styles.constEnd())
        return;
    QSettings settings(m_statePath, QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("plugins/3dview/trajectoryStyles"));
    settings.beginGroup(styleGroup(trackName));
    settings.setValue(QStringLiteral("visible"), it.value().visible);
    settings.setValue(QStringLiteral("color"), it.value().color);
    settings.setValue(QStringLiteral("width"), it.value().width);
    settings.setValue(QStringLiteral("pattern"), static_cast<int>(it.value().pattern));
}

void View3DWidget::updateFrame(qsizetype frame)
{
    const bool radians = m_config.angleUnit == QStringLiteral("rad");
    if (m_repository.tcpTrack(m_activePoseTrack))
    {
        m_viewport->setTcpPose(view3d::makeTcpPoseTransform(
            m_repository.tcpPosition(m_activePoseTrack, frame),
            m_repository.tcpEulerZyx(m_activePoseTrack, frame),
            radians), true);
    }
    else
    {
        m_viewport->setTcpPose({}, false);
    }

    QVector<QMatrix4x4> jointTransforms;
    const view3d::MappedJointTrack* jointTrack =
        m_repository.jointTrack(m_activeJointTrack);
    if (jointTrack)
    {
        QVector<view3d::JointVariableConfig> variables;
        variables.reserve(jointTrack->channels.size());
        for (const view3d::MappedJointChannel& channel : jointTrack->channels)
            variables.push_back({channel.name, channel.type});
        jointTransforms = view3d::evaluateJointChain(
            variables,
            m_config.model.links,
            m_repository.jointValues(m_activeJointTrack, frame),
            radians);
    }
    else
    {
        QVector<view3d::JointVariableConfig> variables =
            m_config.jointTracks.value(m_activeJointTrack);
        if (variables.isEmpty() && !m_config.jointTracks.isEmpty())
            variables = m_config.jointTracks.constBegin().value();
        jointTransforms = view3d::evaluateJointChain(
            variables, m_config.model.links,
            QVector<double>(variables.size(), 0.0), radians);
    }
    m_viewport->setJointFrames(jointTransforms);
    m_viewport->setMeshTransforms(jointTransforms);

    const qsizetype count = m_playback.frameCount();
    m_frameLabel->setText(count > 0
        ? QStringLiteral("%1 / %2  (%3 s)")
              .arg(frame + 1).arg(count)
              .arg(m_repository.timeAt(frame), 0, 'f', 3)
        : QStringLiteral("0 / 0"));
}

void View3DWidget::setFrame(qsizetype frame)
{
    m_playback.setFrame(frame);
    m_savedFrame = m_playback.frame();
    const QSignalBlocker blocker(m_frameSlider);
    m_frameSlider->setValue(static_cast<int>(std::min<qsizetype>(
        m_playback.frame(), std::numeric_limits<int>::max())));
    updateFrame(m_playback.frame());
}

void View3DWidget::startPlayback(view3d::PlaybackDirection direction)
{
    if (m_playback.frameCount() <= 0)
        return;
    if (direction == view3d::PlaybackDirection::Forward
        && m_playback.frame() == m_playback.frameCount() - 1)
        setFrame(0);
    else if (direction == view3d::PlaybackDirection::Reverse
             && m_playback.frame() == 0)
        setFrame(m_playback.frameCount() - 1);
    m_playback.setSpeed(m_speedSpin->value());
    m_playback.play(direction);
    m_elapsedTimer.restart();
    m_timer->start();
}

void View3DWidget::pausePlayback()
{
    m_playback.pause();
    m_timer->stop();
}

void View3DWidget::playbackTick()
{
    if (!m_elapsedTimer.isValid())
        m_elapsedTimer.start();
    const double elapsedSeconds = static_cast<double>(m_elapsedTimer.restart()) / 1000.0;
    const qsizetype frame = m_playback.advance(elapsedSeconds);
    m_savedFrame = frame;
    {
        const QSignalBlocker blocker(m_frameSlider);
        m_frameSlider->setValue(static_cast<int>(std::min<qsizetype>(
            frame, std::numeric_limits<int>::max())));
    }
    updateFrame(frame);
    if (!m_playback.isPlaying())
        m_timer->stop();
}
