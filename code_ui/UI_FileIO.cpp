#include "UI.h"

#include <qtreewidget.h>
#include <qlabel.h>
#include <qsettings.h>
#include <qcoreapplication.h>
#include <qguiapplication.h>
#include <qsvgrenderer.h>
#include <qpainter.h>
#include <qstylehints.h>
#include <qregularexpression.h>
#include <qfiledialog.h>
#include <qmessagebox.h>
#include <qclipboard.h>
#include <qmenu.h>
#include <qtabbar.h>
#include <qcombobox.h>
#include <qspinbox.h>
#include <qlineedit.h>
#include <qpushbutton.h>
#include <qinputdialog.h>

#include "icons_base64.h"
#include "code_viewer/plotmgr/graph/qcp_column_graph.h"
#include "HighlightDialog.h"
#include "AliasDialog.h"
#include <qdir.h>
#include <qfile.h>
#include <qjsondocument.h>
#include <qjsonarray.h>
#include <qjsonobject.h>

void UI::onLoadCSVClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(
        this,
        "Select CSV files",
        QString(),
        "CSV Files (*.csv);;All Files (*.*)");

    if (files.isEmpty())
        return;

    // Forward the file list to the Viewer's slot for loading
    m_viewer.OnLoadCSV(files);
}

// ============================================================
// exportPlotImage: 导出当前图窗为图片
// ============================================================

void UI::exportPlotImage(int pageIndex)
{
    if (pageIndex < 0 || pageIndex >= m_plotTabs->count())
        return;

    auto* container = m_plotTabs->widget(pageIndex);
    auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
    if (!plot)
        return;

    QString filter = "PNG (*.png);;JPEG (*.jpg);;PDF (*.pdf)";
    QString selectedFilter;
    QString fileName = QFileDialog::getSaveFileName(
        this, QString::fromUtf8("导出图片"), QString(),
        filter, &selectedFilter);

    if (fileName.isEmpty())
        return;

    if (selectedFilter.contains("PNG"))
        plot->savePng(fileName, 0, 0, 2.0, 100);
    else if (selectedFilter.contains("JPEG"))
        plot->saveJpg(fileName, 0, 0, 2.0, 90);
    else if (selectedFilter.contains("PDF"))
        plot->savePdf(fileName);
}
