#pragma once

#include "config/view3d_config.h"
#include "playback/playback_state.h"
#include "render/view3d_scene.h"
#include "trajectory/trajectory_data.h"

#include <QMap>
#include <QString>
#include <QWidget>

class QComboBox;
class QCloseEvent;
class QDoubleSpinBox;
class QLabel;
class QSlider;
class QSplitter;
class QSpinBox;
class QTableWidget;
class QTimer;
class QToolButton;

class View3DWidget final : public QWidget
{
public:
    explicit View3DWidget(
        const QString& configPath,
        const QString& statePath,
        QWidget* parent = nullptr);
    ~View3DWidget() override;

    bool reloadConfiguration();
    void setDataSnapshot(viewer::plugin::DataSnapshotPtr snapshot);
    void clearData(bool resetPlaybackFrame = true);
    QString lastError() const;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void setupUi();
    void restoreUiState();
    void saveUiState() const;
    void rebuildRepository();
    void rebuildScene();
    void loadModels();
    void resetModelPose();
    void rebuildControls();
    void rebuildTrackTable();
    void applyTrajectoryStyles();
    view3d::TrajectoryStyle loadStyle(
        const QString& trackName,
        const view3d::TrajectoryStyle& fallback) const;
    void saveStyle(const QString& trackName) const;
    void updateJointState(qsizetype frame);
    void updateTcpState(qsizetype frame);
    void updateTrajectoryDisplay(qsizetype frame);
    void updateFrame(qsizetype frame);
    void setFrame(qsizetype frame);
    void startPlayback(view3d::PlaybackDirection direction);
    void pausePlayback();
    void playbackTick();

    QString m_configPath;
    QString m_statePath;
    QString m_lastError;
    view3d::View3DConfig m_config;
    view3d::TrajectoryRepository m_repository;
    viewer::plugin::DataSnapshotPtr m_snapshot;
    view3d::View3DScene* m_viewport = nullptr;
    QSplitter* m_splitter = nullptr;
    QComboBox* m_jointCombo = nullptr;
    QComboBox* m_poseCombo = nullptr;
    QComboBox* m_trajectoryModeCombo = nullptr;
    QSpinBox* m_localSamplesSpin = nullptr;
    QTableWidget* m_trackTable = nullptr;
    QToolButton* m_reverseButton = nullptr;
    QToolButton* m_previousButton = nullptr;
    QToolButton* m_pauseButton = nullptr;
    QToolButton* m_nextButton = nullptr;
    QToolButton* m_forwardButton = nullptr;
    QDoubleSpinBox* m_speedSpin = nullptr;
    QSlider* m_frameSlider = nullptr;
    QLabel* m_frameLabel = nullptr;
    QLabel* m_status = nullptr;
    QTimer* m_timer = nullptr;
    view3d::PlaybackState m_playback;
    QMap<QString, view3d::TrajectoryStyle> m_styles;
    QString m_activeJointTrack;
    QString m_activePoseTrack;
    QString m_savedJointTrack;
    QString m_savedPoseTrack;
    QStringList m_modelDiagnostics;
    qsizetype m_savedFrame = 0;
    bool m_configLoaded = false;
    bool m_rebuildingControls = false;
    bool m_controlsInitialized = false;
};
