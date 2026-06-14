#pragma once

#include <QtWidgets/QMainWindow>
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

    QIcon createDpiAwareIcon(const QString& fullstr, int logicalsize = 24);
    void forceStrokeColor(QString& str, const QString& color);

private:
    Ui::UIClass ui;

    ads::CDockManager* m_dockManager;

    viewer::Viewer m_viewer;
};
