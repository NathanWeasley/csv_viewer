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
#include <qabstractitemview.h>
#include <qdiriterator.h>
#include <qlistview.h>
#include <qtreeview.h>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <shobjidl.h>
#pragma comment(lib, "ole32.lib")
#endif

#include "icons_base64.h"
#include "code_viewer/plotmgr/graph/qcp_column_graph.h"
#include "HighlightDialog.h"
#include "AliasDialog.h"
#include <qdir.h>
#include <qfile.h>
#include <qjsondocument.h>
#include <qjsonarray.h>
#include <qjsonobject.h>

#ifdef Q_OS_WIN
static QStringList selectFoldersNativeWin32(QWidget* parent, const QString& title)
{
    QStringList folders;

    HRESULT coInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const bool shouldUninit = SUCCEEDED(coInit);
    if (FAILED(coInit) && coInit != RPC_E_CHANGED_MODE)
        return folders;

    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(hr) && dialog)
    {
        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options)))
        {
            dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_ALLOWMULTISELECT |
                               FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        }

        const std::wstring titleText = title.toStdWString();
        dialog->SetTitle(titleText.c_str());

        HWND hwnd = parent ? reinterpret_cast<HWND>(parent->winId()) : nullptr;
        hr = dialog->Show(hwnd);
        if (SUCCEEDED(hr))
        {
            IShellItemArray* items = nullptr;
            hr = dialog->GetResults(&items);
            if (SUCCEEDED(hr) && items)
            {
                DWORD count = 0;
                items->GetCount(&count);
                for (DWORD i = 0; i < count; ++i)
                {
                    IShellItem* item = nullptr;
                    if (SUCCEEDED(items->GetItemAt(i, &item)) && item)
                    {
                        PWSTR path = nullptr;
                        if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path)
                        {
                            folders << QDir::cleanPath(QString::fromWCharArray(path));
                            CoTaskMemFree(path);
                        }
                        item->Release();
                    }
                }
                items->Release();
            }
        }
        dialog->Release();
    }

    if (shouldUninit)
        CoUninitialize();

    folders.removeDuplicates();
    return folders;
}
#endif

void UI::onLoadCSVClicked()
{
    logFileTrace("CSV file picker enter");
    QStringList files = QFileDialog::getOpenFileNames(
        this,
        "Select CSV files",
        QString(),
        "CSV Files (*.csv);;All Files (*.*)");

    if (files.isEmpty())
    {
        logFileTrace("CSV file picker cancelled");
        return;
    }

    // Forward the file list to the Viewer's slot for loading
    logFileTrace(QString("CSV load dispatch count=%1 first=\"%2\" last=\"%3\"")
                 .arg(files.size()).arg(files.first(), files.last()));
    m_viewer.OnLoadCSV(files);
}

void UI::onLoadFolderClicked()
{
    logFileTrace("CSV folder picker enter");
    QStringList folders;

#ifdef Q_OS_WIN
    folders = selectFoldersNativeWin32(this, QString::fromUtf8("Select CSV folders"));
#else
    QFileDialog dlg(this, QString::fromUtf8("Select CSV folders"));
    dlg.setFileMode(QFileDialog::Directory);
    dlg.setOption(QFileDialog::ShowDirsOnly, true);
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);

    for (auto* view : dlg.findChildren<QAbstractItemView*>())
        view->setSelectionMode(QAbstractItemView::ExtendedSelection);

    if (dlg.exec() != QDialog::Accepted)
        return;

    folders = dlg.selectedFiles();
#endif

    if (folders.isEmpty())
    {
        logFileTrace("CSV folder picker cancelled");
        return;
    }

    logFileTrace(QString("CSV folder scan enter folders=%1").arg(folders.size()));

    QStringList csvFiles;
    for (const QString& folder : folders)
    {
        QDirIterator it(folder,
                        QStringList() << QStringLiteral("*.csv"),
                        QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            const QString filePath = QDir::cleanPath(it.next());
            if (!csvFiles.contains(filePath, Qt::CaseInsensitive))
                csvFiles << filePath;
        }
    }

    csvFiles.sort(Qt::CaseInsensitive);

    if (csvFiles.isEmpty())
    {
        logFileTrace(QString("CSV folder scan leave: no files folders=%1").arg(folders.size()));
        QMessageBox::information(this,
                                 QString::fromUtf8("No CSV files"),
                                 QString::fromUtf8("No CSV files were found in the selected folders."));
        return;
    }

    logFileTrace(QString("CSV folder load dispatch folders=%1 files=%2 first=\"%3\" last=\"%4\"")
                 .arg(folders.size()).arg(csvFiles.size())
                 .arg(csvFiles.first(), csvFiles.last()));
    m_viewer.OnLoadCSV(csvFiles, true);
}

// ============================================================
// exportPlotImage: 导出当前图窗为图片
// ============================================================

void UI::exportPlotImage(int pageIndex)
{
    logFileTrace(QString("plot export enter page=%1 pageCount=%2")
                 .arg(pageIndex).arg(plotPageCount()));
    if (pageIndex < 0 || pageIndex >= plotPageCount())
    {
        logFileTrace("plot export aborted: invalid page");
        return;
    }

    auto* container = getPlotContainer(pageIndex);
    auto* plot = container ? container->findChild<QCustomPlot*>() : nullptr;
    if (!plot)
    {
        logFileTrace("plot export aborted: plot missing");
        return;
    }

    QString filter = "PNG (*.png);;JPEG (*.jpg);;PDF (*.pdf)";
    QString selectedFilter;
    QString fileName = QFileDialog::getSaveFileName(
        this, QString::fromUtf8("导出图片"), QString(),
        filter, &selectedFilter);

    if (fileName.isEmpty())
    {
        logFileTrace("plot export cancelled");
        return;
    }

    bool saved = false;
    if (selectedFilter.contains("PNG"))
        saved = plot->savePng(fileName, 0, 0, 2.0, 100);
    else if (selectedFilter.contains("JPEG"))
        saved = plot->saveJpg(fileName, 0, 0, 2.0, 90);
    else if (selectedFilter.contains("PDF"))
        saved = plot->savePdf(fileName);
    logFileTrace(QString("plot export leave page=%1 format=\"%2\" path=\"%3\" success=%4")
                 .arg(pageIndex).arg(selectedFilter, fileName).arg(saved));
}
