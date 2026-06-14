#include "UI.h"
#include <QtWidgets/QApplication>
#include <qcoreapplication.h>
#include <qsettings.h>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 1. Force the global format to INI
    QSettings::setDefaultFormat(QSettings::IniFormat);

    // 2. Point the user configuration storage path directly to the EXE folder
    QString exeDir = QCoreApplication::applicationDirPath();
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, exeDir);

    // 3. Define the company and app names
    a.setOrganizationName("WeekendBuild");
    a.setApplicationName("MyViewer");

    a.setApplicationDisplayName("Viewer V1.0");

    UI w;
    w.setWindowTitle("Viewer V1.0");
    w.show();
    return a.exec();
}
