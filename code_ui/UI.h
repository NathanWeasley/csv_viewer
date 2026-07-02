#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QProgressBar>
#include <QtWidgets/QTreeWidget>
#include <QtWidgets/QTabWidget>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QSpinBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include <QEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QHash>
#include <QLabel>
#include <QPoint>
#include <map>
#include <unordered_map>
#include "ui_UI.h"
#include "DockManager.h"
#include "code_viewer/viewer/viewer_lib.h"
#include "code_viewer/plotmgr/graph/qcp_column_graph.h"
#include "code_viewer/plotmgr/highlight/highlight_manager.h"
#include "code_viewer/plotmgr/fft/fft_manager.h"
#include "code_viewer/stylemgr/style_manager.h"
#include "code_qcp/qcustomplot.h"

class HighlightDialog;
class UI
    : public QMainWindow
{
    Q_OBJECT

public:
    UI(QWidget *parent = nullptr);
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

    void closeEvent(QCloseEvent* event);
    void saveState();

private Q_SLOTS:
    /** Open file dialog for selecting CSV files */
    void onLoadCSVClicked();

    /** Data tree item double-clicked → add to active plot */
    void onDataItemDoubleClicked(QTreeWidgetItem* item, int column);

    QIcon createDpiAwareIcon(const QString& fullstr, int logicalsize = 24);
    void forceStrokeColor(QString& str, const QString& color);

    // ---- Cursor Manager helpers ----

    // 查找鼠标最近的数据点，返回 (graph, dataIndex) pair；未找到返回 (nullptr, 0)
    // 内部使用二分查找 O(log N)，不受自适应降采样影响，始终返回原始全量数据中的最近点
    QPair<viewer::QCPColumnGraph*, size_t> findNearestDataPoint(
        QCustomPlot* plot, const QPoint& mousePos, double pixelThreshold = 10.0) const;

    void refreshCursorLabelStyle(int cursorIdx, bool active);

    // ---- Highlight Manager helpers ----

    // 显示高亮规则配置对话框
    void showHighlightDialog(int pageIndex);

    // 根据高亮规则渲染高亮色块和文字标注
    void renderHighlights(int pageIndex);

    // 导出当前图窗为图片（PNG/JPEG/PDF）
    void exportPlotImage(int pageIndex);

    // ---- Alias ----
    void loadAliasFile();
    void saveAliasFile();
    void showAliasDialog();

    // ---- Bookmark ----
    void loadBookmarkFile();
    void saveBookmarkFile();
    void addBookmark(int pageIndex);
    void onBookmarkDoubleClicked(QTreeWidgetItem* item, int column);
    void onBookmarkTreeContextMenu(const QPoint& pos);
    void rebuildBookmarkTree();
    void restoreBookmark(const viewer::BookmarkEntry& entry);

    // ---- FFT ----
    void onFFTRequested(int pageIndex);
    void showFFTDialog(int pageIndex, double xMin, double xMax);
    void cancelFFTSelection();

private:
    Ui::UIClass ui;

    ads::CDockManager* m_dockManager;

    QProgressBar* m_progressBar = nullptr;

    QTreeWidget* m_dataTree = nullptr;
    QTreeWidget* m_bookmarkTree = nullptr;
    ads::CDockWidget* m_plotDock = nullptr;
    ads::CDockWidget* m_dataDock = nullptr;
    ads::CDockWidget* m_bookmarkDock = nullptr;

    QLabel* m_xAxisLabel = nullptr;

    // Plot management (Qt widgets, logic state in Viewer::m_plots)
    QTabWidget* m_plotTabs = nullptr;

    // ---- Cursor state ----

    // 状态栏游标文本标签
    QLabel* m_cursorStatusLabel = nullptr;

    // plot → pageIndex 映射（因 plot 嵌套在 container 中，不是 tab 直接子级）
    QHash<QCustomPlot*, int> m_plotToPageIndex;

    // 预选追踪器（每个plot共用，生命周期由onPageAdded/onPageAboutToRemove管理）
    // 存储为：plot指针 → tracer指针
    QHash<QCustomPlot*, QCPItemTracer*> m_preSelTracers;

    // 游标浮窗（cursorIndex → tooltip widget）
    QHash<int, QCPItemText*> m_cursorLabels;

    // 游标 tracer 标记（cursorIndex → QCPItemTracer），常驻 plot 上
    QHash<int, QCPItemTracer*> m_cursorTracers;

    // 鼠标按下位置（用于区分拖拽和点击）
    QPoint m_mousePressPos;
    bool   m_mousePressOnPlot = false;

    // ---- Expression bar state ----
    // pageIndex → expression QLineEdit widget
    QHash<int, QLineEdit*> m_exprLineEdits;
    // pageIndex → toolbar QComboBox (for star updating)
    QHash<int, QComboBox*> m_toolbarCombos;

    // ---- Alias state ----
    std::unordered_map<std::string, std::string> m_aliasMap;

    // ---- Highlight state ----
    // pageIndex → 高亮色块列表
    QHash<int, QList<QCPItemRect*>> m_highlightRects;
    // pageIndex → 文字标注列表
    QHash<int, QList<QCPItemText*>> m_highlightLabels;
    // pageIndex → afterReplot 连接（用于缩放时动态更新高亮区域 Y 范围）
    QHash<int, QMetaObject::Connection> m_highlightReplotConns;

    // ---- FFT state ----
    bool m_fftSelecting = false;          // 是否处于 FFT 框选模式
    int  m_fftPageIndex = -1;             // 发起 FFT 的图窗索引
    QPoint m_fftSelStart;                 // 框选起始像素坐标
    QPoint m_fftSelEnd;                   // 框选结束像素坐标
    QCPItemRect* m_fftSelectRect = nullptr; // 框选矩形叠加层

    // FFT 结果列生命周期（pageIndex → unique_ptr）
    std::unordered_map<int, std::unique_ptr<viewer::Column>> m_fftMagCols;
    std::unordered_map<int, std::unique_ptr<viewer::Column>> m_fftFreqCols;

    // ---- Settings state ----
    bool m_adaptiveDownsampling = true;  // 自适应降采样开关
    bool m_openglEnabled = true;          // OpenGL 绘制开关
    bool m_antiAliasingEnabled = false;   // 抗锯齿开关

    viewer::Viewer m_viewer;
};