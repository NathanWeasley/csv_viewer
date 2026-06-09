#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_UI.h"

#include "DockManager.h"

class UI
    : public QMainWindow
{
    Q_OBJECT

public:
    UI(QWidget *parent = nullptr);
    ~UI();

private:
    void init();

private:
    Ui::UIClass ui;

    ads::CDockManager* m_dockManager;
};
