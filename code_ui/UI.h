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
#include "ui_UI.h"
#include "DockManager.h"
#include "code_viewer/viewer/viewer_lib.h"
#include "code_viewer/datamgr/qcp_chunked_graph.h"
#include "code_qcp/qcustomplot.h"

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

private Q_SLOTS:
    /** Open file dialog for selecting CSV files */
    void onLoadCSVClicked();

    /** Data tree item double-clicked → add to active plot */
    void onDataItemDoubleClicked(QTreeWidgetItem* item, int column);

    QIcon createDpiAwareIcon(const QString& fullstr, int logicalsize = 24);
    void forceStrokeColor(QString& str, const QString& color);

    // ---- Cursor Manager helpers ----

    // 查找鼠标最近的数据点，返回 (graph, dataIndex) pair；未找到返回 (nullptr, 0)
    QPair<viewer::QCPChunkedGraph*, size_t> findNearestDataPoint(
        QCustomPlot* plot, const QPoint& mousePos, double pixelThreshold = 10.0) const;

    // 检查数据密度是否过高（dataCount / plotWidth > 1.0 时跳过预选）
    bool isDataTooDense(QCustomPlot* plot) const;

    // 更新指定游标的浮窗位置
    void updateCursorTooltipPosition(int cursorIdx);

private:
    Ui::UIClass ui;

    ads::CDockManager* m_dockManager;

    QProgressBar* m_progressBar = nullptr;

    QTreeWidget* m_dataTree = nullptr;
    ads::CDockWidget* m_plotDock = nullptr;
    ads::CDockWidget* m_dataDock = nullptr;

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
    QHash<int, QWidget*> m_cursorTooltips;

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

    viewer::Viewer m_viewer;
};