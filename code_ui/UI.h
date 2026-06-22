#pragma once

#include <QtWidgets/QMainWindow>
#include <QtWidgets/QProgressBar>
#include "ui_UI.h"
#include "DockManager.h"
#include "code_viewer/viewer/viewer_lib.h"

class UI
    : public QMainWindow
{
    Q_OBJECT

public:
    UI(QWidget *parent = nullptr);
    ~UI();

private:
    void init();

    void createMenu();
    void createToolbar();
    void createMain();
    void createStatusbar();

    void closeEvent(QCloseEvent* event);

private Q_SLOTS:
    /** Open file dialog for selecting CSV files */
    void onLoadCSVClicked();

    QIcon createDpiAwareIcon(const QString& fullstr, int logicalsize = 24);
    void forceStrokeColor(QString& str, const QString& color);

private:
    Ui::UIClass ui;

    ads::CDockManager* m_dockManager;

    QProgressBar* m_progressBar = nullptr;

    viewer::Viewer m_viewer;
};
