#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_UI.h"

class UI : public QMainWindow
{
    Q_OBJECT

public:
    UI(QWidget *parent = nullptr);
    ~UI();

private:
    Ui::UIClass ui;
};
