#include "UI.h"
#include <QtWidgets/QApplication>
#include <qcoreapplication.h>
#include <qsettings.h>

#include "code_viewer/base/trace_logger.h"

#ifdef Q_OS_WIN
// Hybrid-graphics laptops often create off-screen OpenGL contexts on the
// integrated GPU even when an NVIDIA GPU is installed. These well-known
// exports ask the NVIDIA Optimus / AMD PowerXpress driver to start this
// process on the high-performance adapter. They must live in the executable,
// and therefore belong in main.cpp rather than one of the DLL projects.
extern "C"
{
Q_DECL_EXPORT unsigned long NvOptimusEnablement = 0x00000001;
Q_DECL_EXPORT int AmdPowerXpressRequestHighPerformance = 1;
}
#endif

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Force the global format to INI
    QSettings::setDefaultFormat(QSettings::IniFormat);

    // Keep diagnostics bounded to the current run. This also clears disabled logs.
    viewer::trace::initializeSessionFiles();
    viewer::trace::write(viewer::trace::Category::Operation, "application startup");

    a.setApplicationDisplayName("Viewer V1.0");

    UI w;
    w.setWindowTitle("Viewer V1.0");
    w.show();
    return a.exec();
}
