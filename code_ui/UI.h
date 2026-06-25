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
#include <map>
#include "ui_UI.h"
#include "DockManager.h"
#include "code_viewer/viewer/viewer_lib.h"
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

    void closeEvent(QCloseEvent* event);

private Q_SLOTS:
    /** Open file dialog for selecting CSV files */
    void onLoadCSVClicked();

    /** Data tree item double-clicked → add to active plot */
    void onDataItemDoubleClicked(QTreeWidgetItem* item, int column);

    QIcon createDpiAwareIcon(const QString& fullstr, int logicalsize = 24);
    void forceStrokeColor(QString& str, const QString& color);

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

    viewer::Viewer m_viewer;
};