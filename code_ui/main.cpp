#include "UI.h"
#include <QtWidgets/QApplication>
#include <qcoreapplication.h>
#include <qsettings.h>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Force the global format to INI
    QSettings::setDefaultFormat(QSettings::IniFormat);

    a.setApplicationDisplayName("Viewer V1.0");

    UI w;
    w.setWindowTitle("Viewer V1.0");
    w.show();
    return a.exec();
}
