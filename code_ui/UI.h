#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include <QAction>
#include <QByteArray>
#include <QEvent>
#include <QElapsedTimer>
#include <QHash>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPoint>
#include <QSet>
#include <QWheelEvent>
#include <map>
#include <atomic>
#include <memory>
#include <unordered_map>

#include "ui_UI.h"
#include "AboutDialog.h"
#include "DockManager.h"
#include "code_qcp/qcustomplot.h"
#include "code_viewer/plotmgr/fft/fft_manager.h"
#include "code_viewer/plotmgr/graph/qcp_column_graph.h"
#include "code_viewer/plotmgr/highlight/highlight_manager.h"
#include "code_viewer/stylemgr/style_manager.h"
#include "code_viewer/viewer/viewer_lib.h"

class HighlightDialog;
namespace ads { class CDockWidget; }

class UI : public QMainWindow
{
    Q_OBJECT

public:
    UI(QWidget* parent = nullptr);
    ~UI();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void init();

    void createMenu();
    void createToolbar();
    void createMain();
    void createStatusbar();
    void bindPlotManagerCallbacks();
    void bindCursorManagerCallbacks();

    QCustomPlot* getPlot(int pageIndex) const;
    QWidget* getPlotContainer(int pageIndex) const;
    int plotPageCount() const;
    int activePlotPage() const;
    ads::CDockWidget* addPlotPageDock(int pageIndex, QWidget* container, const QString& title);
    void removePlotPageDock(int pageIndex);

    void cleanupPlotPageState(int pageIndex, ads::CDockWidget* dock = nullptr);
    void reindexPlotPageStateAfterRemoval(int removedPageIndex);

    void connectInnerDockSignals();
    void hideDockAreaTitleBar(ads::CDockWidget* dock);
    void hideFixedDockTitleBars();

    void closeEvent(QCloseEvent* event);
    void beginShutdownCleanup(bool persistUiState);
    void saveState();
    void cleanupPlotOverlaysBeforeShutdown();
    void disconnectViewerCallbacks();
    void removeAllPlotDocksForShutdown();
    void logShutdownTrace(const QString& message) const;
    void logPlotTrace(const QString& message) const;
    void logOperationTrace(const QString& message) const;
    void logFileTrace(const QString& message) const;
    void logBookmarkTrace(const QString& message) const;
    void logLayoutTrace(const QString& message) const;
    void logXAxisTrace(const QString& message) const;
    void logExpressionTrace(const QString& message) const;
    bool configurePlotDrawingMode(QCustomPlot* plot, bool enabled) const;
    void applyOpenGlDrawingMode(bool enabled);
    bool selectPageXAxis(int pageIndex, size_t* selectedColumn, const QString& prompt);
    bool resolvePageXAxis(int pageIndex, size_t* selectedColumn, bool promptIfMissing);
    void setPageUseIndexEnabled(int pageIndex, bool enabled, QCheckBox* sourceCheckBox = nullptr);
    void updatePageXAxisToolbarState(int pageIndex);
    void updateXAxisStatus(int pageIndex);
    void applyPlotLayoutMode(viewer::PlotLayoutMode mode);
    void restoreTabbedPlotLayout();
    void arrangePlotsInRowLayout();
    void arrangePlotsInGridLayout();
    void normalizeAllPlotDocksToMainContainer();

    void rebuildDataTree();
    void onDataTreeContextMenu(const QPoint& pos);
    void plotDataColumnByName(const QString& dataName);
    void plotDataGroupInNewPage(QTreeWidgetItem* groupItem);
    bool isDataGroupingEnabledForDisplay() const;

    void showLinkXAxisDialog();
    QString plotPageDisplayName(int pageIndex) const;
    bool isLinkEligiblePlotPage(int pageIndex) const;
    int linkedXAxisGroupIndexForPage(int pageIndex) const;
    void syncLinkedXAxisRange(int sourcePageIndex, const QCPRange& newRange);
    void cleanupLinkedXAxisGroups();
    void reindexLinkedXAxisGroupsAfterRemoval(int removedPageIndex);

    void updatePlotLayoutActions(viewer::PlotLayoutMode mode);
    void updatePlotPageChromeForLayout(viewer::PlotLayoutMode mode);
    void setPlotPageBaseChrome(int pageIndex, bool toolbarVisible, bool exprVisible);
    void clearLayoutPlaceholders();

private Q_SLOTS:
    void onLoadCSVClicked();
    void onLoadFolderClicked();
    void onLoadHiklogClicked();
    void onDataItemDoubleClicked(QTreeWidgetItem* item, int column);

    QPair<viewer::QCPColumnGraph*, size_t> findNearestDataPoint(
        QCustomPlot* plot, const QPoint& mousePos, double pixelThreshold = 10.0) const;

    void refreshCursorLabelStyle(int cursorIdx, bool active);
    QCPItemText* hitCursorLabel(int pageIndex, const QPoint& pos, int* cursorIdx = nullptr) const;
    void updateCursorConnectorLine(int cursorIdx);

    void showHighlightDialog(int pageIndex);
    void renderHighlights(int pageIndex);
    void exportPlotImage(int pageIndex);

    void loadAliasFile();
    void saveAliasFile();
    void showAliasDialog();

    void loadBookmarkFile();
    void saveBookmarkFile();
    void addBookmark(int pageIndex);
    void onBookmarkDoubleClicked(QTreeWidgetItem* item, int column);
    void onBookmarkTreeContextMenu(const QPoint& pos);
    void rebuildBookmarkTree();
    void restoreBookmark(const viewer::BookmarkEntry& entry);

    void onFFTRequested(int pageIndex);
    void showFFTDialog(int pageIndex, double xMin, double xMax);
    void cancelFFTSelection();
    void onSTFTRequested(int pageIndex);
    void showSTFTDialog(int pageIndex);

private:
    Ui::UIClass ui;

    ads::CDockManager* m_dockManager = nullptr;

    QProgressBar* m_progressBar = nullptr;

    QTreeWidget* m_dataTree = nullptr;
    QTreeWidget* m_bookmarkTree = nullptr;
    ads::CDockWidget* m_plotDock = nullptr;
    ads::CDockWidget* m_dataDock = nullptr;
    ads::CDockWidget* m_bookmarkDock = nullptr;

    QLabel* m_xAxisLabel = nullptr;
    QLabel* m_cursorStatusLabel = nullptr;

    // Inner plot dock state
    ads::CDockManager* m_plotDockManager = nullptr;
    QHash<int, ads::CDockWidget*> m_pageDocks;
    bool m_innerDockSignalsConnected = false;
    int m_pendingActivation = -1;
    int m_lastRemovedPageIndex = -1;
    bool m_syncingPlotRemoval = false;
    bool m_rearrangingPlotLayout = false;
    bool m_isShuttingDown = false;
    bool m_shutdownCleanupDone = false;
    int m_pendingDockTargetPage = -1;
    ads::DockWidgetArea m_pendingDockArea = ads::CenterDockWidgetArea;
    QByteArray m_savedTabbedPlotLayoutState;
    bool m_hasSavedTabbedPlotLayoutState = false;
    QAction* m_actionNewPlot = nullptr;
    QAction* m_actionGridView = nullptr;
    QAction* m_actionRowView = nullptr;
    QAction* m_actionLinkX = nullptr;
    QAction* m_actionAutoGrouping = nullptr;
    QAction* m_actionOpenGl = nullptr;
    QAction* m_actionPlotByIndex = nullptr;
    QList<ads::CDockWidget*> m_layoutPlaceholderDocks;
    QHash<int, bool> m_pageToolbarBaseVisible;
    QHash<int, bool> m_pageExprBaseVisible;

    // X-axis link groups: each inner list is one group of page indices
    QList<QList<int>> m_linkedXAxisGroups;
    bool m_syncingLinkedXAxis = false;

    // Cursor state
    QHash<QCustomPlot*, int> m_plotToPageIndex;
    QHash<QCustomPlot*, QCPItemTracer*> m_preSelTracers;
    QHash<int, QCPItemText*> m_cursorLabels;
    QHash<int, QCPItemTracer*> m_cursorTracers;
    QHash<int, QCPItemLine*> m_cursorConnectorLines;
    QPoint m_mousePressPos;
    bool m_mousePressOnPlot = false;
    bool m_clearingAllPlots = false;
    int m_draggingCursorLabelIdx = -1;
    QPointF m_cursorLabelDragOffset;

    // Expression bar state
    QHash<int, QLineEdit*> m_exprLineEdits;
    QHash<int, QComboBox*> m_toolbarCombos;
    QHash<int, QCheckBox*> m_useIndexChecks;

    // Alias state
    std::unordered_map<std::string, std::string> m_aliasMap;
    QStringList m_pendingSkippedFiles;
    bool m_binaryLogLoading = false;
    std::shared_ptr<std::atomic_bool> m_binaryLogCancel;
    bool m_binaryLogProgressActive = false;
    quint64 m_binaryLogProgressGeneration = 0;
    QElapsedTimer m_binaryLogProgressTimer;

    // Highlight state
    QHash<int, QList<QCPItemRect*>> m_highlightRects;
    QHash<int, QList<QCPItemText*>> m_highlightLabels;
    QHash<int, QMetaObject::Connection> m_highlightReplotConns;

    // FFT state
    bool m_fftSelecting = false;
    int m_fftPageIndex = -1;
    QPoint m_fftSelStart;
    QPoint m_fftSelEnd;
    QCPItemRect* m_fftSelectRect = nullptr;
    std::unordered_map<int, std::unique_ptr<viewer::Column>> m_fftMagCols;
    std::unordered_map<int, std::unique_ptr<viewer::Column>> m_fftFreqCols;

    // Settings state
    bool m_adaptiveDownsampling = true;
    bool m_openglEnabled = true;
    bool m_antiAliasingEnabled = true;
    bool m_autoGroupingEnabled = false;
    bool m_forceDataGrouping = false;
    bool m_defaultPlotByIndex = true;

    viewer::Viewer m_viewer;
};
