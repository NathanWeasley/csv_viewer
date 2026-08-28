#include "UI.h"
#include "RbtLogViewer.h"

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
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QFont>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QIcon>
#include <QListWidget>
#include <QPointer>
#include <QSplitter>
#include <QToolButton>
#include <QtConcurrent/QtConcurrentRun>

#include <atomic>
#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

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

#include "code_logparse/binary_log_types.h"

namespace
{

constexpr int kZipEntryIndexRole = Qt::UserRole + 40;
constexpr int kZipEntryKindRole = Qt::UserRole + 41;
constexpr int kZipEntryKindHiklog = 1;
constexpr int kZipEntryKindRbt = 2;

struct ZipLogLoadResult
{
    viewer::logparse::ParseResult hiklog;
    viewer::logparse::RbtLogBatchResult rbt;
    bool hasHiklog = false;
    bool cancelled = false;
};

QString formatByteCount(uint64_t bytes)
{
    static const char* units[] = {"B", "KiB", "MiB", "GiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3)
    {
        value /= 1024.0;
        ++unit;
    }
    return unit == 0
        ? QString::number(bytes) + QStringLiteral(" B")
        : QString::number(value, 'f', 1) + QStringLiteral(" ")
            + QString::fromLatin1(units[unit]);
}

QString zipEntryStatus(const viewer::logparse::ziplog::ZipEntryInfo& entry)
{
    if (entry.hasUnsafePath)
        return QString::fromUtf8(u8"不安全路径");
    if (entry.isSymlink)
        return QString::fromUtf8(u8"符号链接");
    if (entry.isEncrypted())
        return QString::fromUtf8(u8"已加密");
    if (!entry.compressionSupported)
        return QString::fromUtf8(u8"不支持的压缩方式");
    return entry.isDirectory ? QString() : QString::fromUtf8(u8"可读");
}

class ZipLogSelectionDialog final : public QDialog
{
public:
    ZipLogSelectionDialog(const std::filesystem::path& archivePath,
                          const std::vector<viewer::logparse::ziplog::ZipEntryInfo>& entries,
                          QWidget* parent)
        : QDialog(parent)
    {
        setWindowTitle(QString::fromUtf8(u8"选择 ZIP 中的日志"));
        resize(1050, 680);

        auto* layout = new QVBoxLayout(this);
        auto* description = new QLabel(
            QString::fromUtf8(u8"压缩包：%1\n勾选要解析的 .hiklog 或文件名以 RBT 开头的日志；右侧仅设置 HikLog 的连续解析顺序。")
                .arg(QDir::toNativeSeparators(
                    QString::fromStdWString(archivePath.wstring()))),
            this);
        description->setWordWrap(true);
        layout->addWidget(description);

        auto* splitter = new QSplitter(Qt::Horizontal, this);
        m_tree = new QTreeWidget(splitter);
        m_tree->setColumnCount(4);
        m_tree->setHeaderLabels({QString::fromUtf8(u8"名称"),
                                 QString::fromUtf8(u8"原始大小"),
                                 QString::fromUtf8(u8"压缩大小"),
                                 QString::fromUtf8(u8"状态")});
        m_tree->setAlternatingRowColors(true);
        m_tree->header()->setSectionResizeMode(QHeaderView::Interactive);
        m_tree->header()->setStretchLastSection(false);
        m_tree->header()->setMinimumSectionSize(72);
        m_tree->setColumnWidth(0, 360);
        m_tree->setColumnWidth(1, 110);
        m_tree->setColumnWidth(2, 110);
        m_tree->setColumnWidth(3, 120);

        auto* orderPanel = new QWidget(splitter);
        auto* orderLayout = new QVBoxLayout(orderPanel);
        orderLayout->addWidget(new QLabel(
            QString::fromUtf8(u8"HikLog 连续解析顺序（首文件定义主结构）"), orderPanel));
        m_order = new QListWidget(orderPanel);
        m_order->setAlternatingRowColors(true);
        orderLayout->addWidget(m_order, 1);

        auto* buttonRow = new QHBoxLayout();
        auto* moveUp = new QPushButton(QString::fromUtf8(u8"上移"), orderPanel);
        auto* moveDown = new QPushButton(QString::fromUtf8(u8"下移"), orderPanel);
        auto* remove = new QPushButton(QString::fromUtf8(u8"移除"), orderPanel);
        buttonRow->addWidget(moveUp);
        buttonRow->addWidget(moveDown);
        buttonRow->addWidget(remove);
        orderLayout->addLayout(buttonRow);
        splitter->addWidget(m_tree);
        splitter->addWidget(orderPanel);
        splitter->setStretchFactor(0, 3);
        splitter->setStretchFactor(1, 2);
        layout->addWidget(splitter, 1);

        m_summary = new QLabel(this);
        layout->addWidget(m_summary);
        m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                                         Qt::Horizontal, this);
        m_buttons->button(QDialogButtonBox::Ok)->setText(QString::fromUtf8(u8"开始解析"));
        layout->addWidget(m_buttons);

        QHash<QString, QTreeWidgetItem*> directories;
        const QIcon folderIcon = style()->standardIcon(QStyle::SP_DirIcon);
        const QIcon fileIcon = style()->standardIcon(QStyle::SP_FileIcon);

        const auto ensureDirectory = [&](const QStringList& components, int count)
        {
            QTreeWidgetItem* parentItem = nullptr;
            QString key;
            for (int part = 0; part < count; ++part)
            {
                if (!key.isEmpty())
                    key += '/';
                key += components[part];
                auto found = directories.constFind(key);
                if (found != directories.constEnd())
                {
                    parentItem = found.value();
                    continue;
                }
                auto* item = parentItem
                    ? new QTreeWidgetItem(parentItem)
                    : new QTreeWidgetItem(m_tree);
                item->setText(0, components[part]);
                item->setIcon(0, folderIcon);
                directories.insert(key, item);
                parentItem = item;
            }
            return parentItem;
        };

        for (const auto& entry : entries)
        {
            QString path = QString::fromUtf8(entry.pathUtf8.c_str());
            const QStringList components = path.split('/', Qt::SkipEmptyParts);
            if (components.isEmpty())
                continue;
            if (entry.isDirectory)
            {
                ensureDirectory(components, components.size());
                continue;
            }

            QTreeWidgetItem* parentItem = ensureDirectory(components, components.size() - 1);
            auto* item = parentItem
                ? new QTreeWidgetItem(parentItem)
                : new QTreeWidgetItem(m_tree);
            item->setText(0, components.back());
            item->setText(1, formatByteCount(entry.uncompressedSize));
            item->setText(2, formatByteCount(entry.compressedSize));
            item->setText(3, zipEntryStatus(entry));
            item->setIcon(0, fileIcon);
            item->setData(0, kZipEntryIndexRole,
                          QVariant::fromValue<qulonglong>(entry.index));
            m_entryItems.insert(entry.index, item);
            m_entrySizes.insert(entry.index, entry.uncompressedSize);

            const bool isHiklog = path.endsWith(QStringLiteral(".hiklog"),
                                                 Qt::CaseInsensitive);
            const bool isRbt = components.back().startsWith(
                QStringLiteral("RBT"), Qt::CaseInsensitive);
            if (entry.canRead() && (isHiklog || isRbt))
            {
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(0, Qt::Unchecked);
                item->setData(0, kZipEntryKindRole,
                              isHiklog ? kZipEntryKindHiklog : kZipEntryKindRbt);
                QFont candidateFont = item->font(0);
                candidateFont.setBold(true);
                item->setFont(0, candidateFont);
                if (isHiklog)
                {
                    item->setText(3, QStringLiteral("HikLog"));
                    item->setToolTip(0, QString::fromUtf8(u8"可选择的 HikLog 文件"));
                }
                else
                {
                    item->setText(3, QString::fromUtf8(u8"RBT 文本日志"));
                    item->setToolTip(0, QString::fromUtf8(u8"可选择的 RBT 分块压缩文本日志"));
                    m_rbtEntryIndices.push_back(entry.index);
                }
            }
            else
            {
                item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
            }
        }
        m_tree->collapseAll();

        connect(m_tree, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem* item, int column)
            {
                if (column != 0 || !item->data(0, kZipEntryIndexRole).isValid())
                    return;
                const uint64_t index = item->data(0, kZipEntryIndexRole).toULongLong();
                const int kind = item->data(0, kZipEntryKindRole).toInt();
                if (kind == kZipEntryKindHiklog
                    && item->checkState(0) == Qt::Checked)
                {
                    if (findOrderRow(index) < 0)
                        addOrderItem(index, fullTreePath(item));
                }
                else if (kind == kZipEntryKindHiklog)
                {
                    const int row = findOrderRow(index);
                    if (row >= 0)
                        delete m_order->takeItem(row);
                }
                updateSummary();
            });

        connect(moveUp, &QPushButton::clicked, this, [this]() { moveCurrent(-1); });
        connect(moveDown, &QPushButton::clicked, this, [this]() { moveCurrent(1); });
        connect(remove, &QPushButton::clicked, this, [this]()
        {
            const int row = m_order->currentRow();
            if (row < 0)
                return;
            const uint64_t index = m_order->item(row)->data(kZipEntryIndexRole).toULongLong();
            if (auto* treeItem = m_entryItems.value(index, nullptr))
                treeItem->setCheckState(0, Qt::Unchecked);
        });
        connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        updateSummary();
    }

    std::vector<uint64_t> selectedHiklogEntryIndices() const
    {
        std::vector<uint64_t> result;
        result.reserve(static_cast<size_t>(m_order->count()));
        for (int row = 0; row < m_order->count(); ++row)
            result.push_back(m_order->item(row)->data(kZipEntryIndexRole).toULongLong());
        return result;
    }

    std::vector<uint64_t> selectedRbtEntryIndices() const
    {
        std::vector<uint64_t> result;
        result.reserve(m_rbtEntryIndices.size());
        for (const uint64_t index : m_rbtEntryIndices)
        {
            const QTreeWidgetItem* item = m_entryItems.value(index, nullptr);
            if (item && item->checkState(0) == Qt::Checked)
                result.push_back(index);
        }
        return result;
    }

private:
    QString fullTreePath(QTreeWidgetItem* item) const
    {
        QStringList parts;
        while (item)
        {
            parts.prepend(item->text(0));
            item = item->parent();
        }
        return parts.join('/');
    }

    int findOrderRow(uint64_t index) const
    {
        for (int row = 0; row < m_order->count(); ++row)
        {
            if (m_order->item(row)->data(kZipEntryIndexRole).toULongLong() == index)
                return row;
        }
        return -1;
    }

    void addOrderItem(uint64_t index, const QString& path)
    {
        auto* item = new QListWidgetItem(path, m_order);
        item->setData(kZipEntryIndexRole, QVariant::fromValue<qulonglong>(index));
    }

    void moveCurrent(int delta)
    {
        const int row = m_order->currentRow();
        const int target = row + delta;
        if (row < 0 || target < 0 || target >= m_order->count())
            return;
        QListWidgetItem* item = m_order->takeItem(row);
        m_order->insertItem(target, item);
        m_order->setCurrentRow(target);
    }

    void updateSummary()
    {
        uint64_t total = 0;
        for (int row = 0; row < m_order->count(); ++row)
        {
            const uint64_t index = m_order->item(row)->data(kZipEntryIndexRole).toULongLong();
            const uint64_t size = m_entrySizes.value(index, 0);
            total = size > std::numeric_limits<uint64_t>::max() - total
                ? std::numeric_limits<uint64_t>::max() : total + size;
        }
        int rbtCount = 0;
        for (const uint64_t index : m_rbtEntryIndices)
        {
            const QTreeWidgetItem* item = m_entryItems.value(index, nullptr);
            if (!item || item->checkState(0) != Qt::Checked)
                continue;
            ++rbtCount;
            const uint64_t size = m_entrySizes.value(index, 0);
            total = size > std::numeric_limits<uint64_t>::max() - total
                ? std::numeric_limits<uint64_t>::max() : total + size;
        }
        m_summary->setText(QString::fromUtf8(
            u8"已选 %1 个 HikLog、%2 个 RBT 日志，ZIP 内原始数据共 %3")
            .arg(m_order->count()).arg(rbtCount).arg(formatByteCount(total)));
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(
            m_order->count() > 0 || rbtCount > 0);
    }

    QTreeWidget* m_tree = nullptr;
    QListWidget* m_order = nullptr;
    QLabel* m_summary = nullptr;
    QDialogButtonBox* m_buttons = nullptr;
    QHash<qulonglong, QTreeWidgetItem*> m_entryItems;
    QHash<qulonglong, qulonglong> m_entrySizes;
    std::vector<uint64_t> m_rbtEntryIndices;
};

QString parseDiagnosticsText(const viewer::logparse::ParseResult& result)
{
    QStringList lines;
    int shown = 0;
    for (const auto& diagnostic : result.diagnostics)
    {
        if (diagnostic.severity != viewer::logparse::DiagnosticSeverity::Error)
            continue;
        lines << QStringLiteral("%1 @ %2: %3")
                     .arg(QString::fromStdWString(diagnostic.filePath.wstring()))
                     .arg(diagnostic.byteOffset)
                     .arg(QString::fromUtf8(diagnostic.message.c_str()));
        if (++shown == 8)
            break;
    }
    if (lines.isEmpty())
        lines << QString::fromUtf8(u8"未生成可用数据。");
    if (result.diagnostics.size() > static_cast<size_t>(shown))
        lines << QString::fromUtf8(u8"其余诊断已省略。");
    return lines.join('\n');
}

} // namespace

QString UI::rbtLogDirectory() const
{
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(
        QStringLiteral("user/rbt_log"));
}

bool UI::resetRbtLogDirectory(QString* error)
{
    if (m_rbtLogViewer)
        m_rbtLogViewer->releaseFiles();

    const QString applicationDirectory = QDir(
        QCoreApplication::applicationDirPath()).absolutePath();
    const QString target = QDir(rbtLogDirectory()).absolutePath();
    const QString relativeTarget = QDir(applicationDirectory).relativeFilePath(target);
    if (relativeTarget.compare(QStringLiteral("user/rbt_log"),
                               Qt::CaseInsensitive) != 0)
    {
        if (error)
            *error = QString::fromUtf8(u8"RBT 临时目录路径校验失败。");
        return false;
    }

    QDir directory(target);
    if (directory.exists() && !directory.removeRecursively())
    {
        if (error)
            *error = QString::fromUtf8(u8"无法清空 RBT 临时目录，文件可能正被其他软件占用：%1")
                .arg(QDir::toNativeSeparators(target));
        return false;
    }
    if (!QDir().mkpath(target))
    {
        if (error)
            *error = QString::fromUtf8(u8"无法创建 RBT 临时目录：%1")
                .arg(QDir::toNativeSeparators(target));
        return false;
    }
    return true;
}

QStringList UI::availableRbtLogFiles() const
{
    const QString path = rbtLogDirectory();
    if (!QDir().mkpath(path))
        return {};
    QDir directory(path);
    const QFileInfoList files = directory.entryInfoList(
        QStringList{QStringLiteral("*.log"), QStringLiteral("*.txt")},
        QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    QStringList paths;
    paths.reserve(files.size());
    for (const QFileInfo& file : files)
        paths.push_back(file.absoluteFilePath());
    return paths;
}

void UI::onShowRbtLogsClicked()
{
    const QStringList files = availableRbtLogFiles();
    if (files.isEmpty())
    {
        QMessageBox::information(
            this, QString::fromUtf8(u8"RBT 日志快速查看"),
            QString::fromUtf8(u8"当前临时目录中没有已解析的 RBT 日志。"));
        return;
    }
    if (!m_rbtLogViewer)
        m_rbtLogViewer = new RbtLogViewerWindow(this);
    m_rbtLogViewer->openFiles(files);
}

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

void UI::onLoadHiklogClicked()
{
    if (m_binaryLogLoading)
    {
        statusBar()->showMessage(QString::fromUtf8(u8"ZIP 日志正在解析中。"), 3000);
        return;
    }

    const QString selectedArchive = QFileDialog::getOpenFileName(
        this,
        QString::fromUtf8(u8"选择包含 HikLog 或 RBT 日志的 ZIP 文件"),
        QString(),
        QString::fromUtf8(u8"ZIP 压缩包 (*.zip);;所有文件 (*.*)"));
    if (selectedArchive.isEmpty())
        return;

    const std::filesystem::path archivePath(selectedArchive.toStdWString());
    m_progressBar->setRange(0, 0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFormat(QString::fromUtf8(u8"正在读取 ZIP 目录…"));
    m_progressBar->setMinimumWidth(180);
    m_progressBar->show();
    statusBar()->showMessage(QString::fromUtf8(u8"正在读取 ZIP 目录结构…"));
    statusBar()->repaint();
    m_progressBar->repaint();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    logFileTrace(QString("ZIP catalog read started archive=\"%1\" progressVisible=%2")
                 .arg(selectedArchive).arg(m_progressBar->isVisible()));
    std::vector<viewer::logparse::ziplog::ZipEntryInfo> entries;
    std::string catalogError;
    const bool catalogOk = viewer::Viewer::ReadZipCatalog(
        archivePath, entries, catalogError);
    logFileTrace(QString("ZIP catalog read finished archive=\"%1\" success=%2 entries=%3")
                 .arg(selectedArchive).arg(catalogOk).arg(entries.size()));
    m_progressBar->hide();
    m_progressBar->setRange(0, 1000);
    m_progressBar->setValue(0);
    m_progressBar->setFormat(QStringLiteral("%p%"));
    if (!catalogOk)
    {
        QMessageBox::critical(
            this,
            QString::fromUtf8(u8"ZIP 打开失败"),
            QString::fromUtf8(catalogError.c_str()));
        return;
    }

    ZipLogSelectionDialog selectionDialog(archivePath, entries, this);
    if (selectionDialog.exec() != QDialog::Accepted)
        return;
    const std::vector<uint64_t> hiklogIndices =
        selectionDialog.selectedHiklogEntryIndices();
    const std::vector<uint64_t> rbtIndices =
        selectionDialog.selectedRbtEntryIndices();
    if (hiklogIndices.empty() && rbtIndices.empty())
        return;

    QString resetError;
    if (!resetRbtLogDirectory(&resetError))
    {
        QMessageBox::critical(
            this, QString::fromUtf8(u8"RBT 临时目录不可用"), resetError);
        return;
    }

    std::unordered_map<uint64_t, uint64_t> sizeByIndex;
    for (const auto& entry : entries)
        sizeByIndex.emplace(entry.index, entry.uncompressedSize);
    auto hiklogSizes = std::make_shared<std::vector<uint64_t>>();
    auto rbtSizes = std::make_shared<std::vector<uint64_t>>();
    hiklogSizes->reserve(hiklogIndices.size());
    rbtSizes->reserve(rbtIndices.size());
    uint64_t rbtBytes = 0;
    for (const uint64_t index : rbtIndices)
    {
        const uint64_t size = sizeByIndex[index];
        rbtSizes->push_back(size);
        rbtBytes += size;
    }
    uint64_t hiklogBytes = 0;
    for (const uint64_t index : hiklogIndices)
    {
        const uint64_t size = sizeByIndex[index];
        hiklogSizes->push_back(size);
        hiklogBytes += size;
    }
    const uint64_t totalBytes = rbtBytes + hiklogBytes;

    auto cancelled = std::make_shared<std::atomic_bool>(false);
    m_binaryLogCancel = cancelled;
    m_binaryLogLoading = true;
    m_binaryLogProgressActive = true;
    ++m_binaryLogProgressGeneration;
    m_binaryLogProgressTimer.restart();
    m_progressBar->setRange(0, 1000);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(true);
    m_progressBar->setFormat(QString::fromUtf8(u8"ZIP 日志 %p%"));
    m_progressBar->setMinimumWidth(180);
    m_progressBar->show();
    statusBar()->showMessage(QString::fromUtf8(u8"正在解析 ZIP 中的日志…"));
    statusBar()->repaint();
    m_progressBar->repaint();
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    const QPointer<UI> uiGuard(this);
    auto lastProgressBucket = std::make_shared<std::atomic_int>(-1);
    const auto postProgress =
        [uiGuard, totalBytes, lastProgressBucket](uint64_t completed,
                                                  const QString& stage,
                                                  const QString& sourceText)
    {
        if (!uiGuard)
            return;
        const int value = totalBytes == 0 ? 0 : static_cast<int>(
            std::min<long double>(1000.0L,
                static_cast<long double>(completed) * 1000.0L
                    / static_cast<long double>(totalBytes)));
        const int bucket = value / 100;
        const int previousBucket = lastProgressBucket->exchange(
            bucket, std::memory_order_relaxed);
        const bool logProgress = bucket != previousBucket;
        QMetaObject::invokeMethod(uiGuard.data(),
            [uiGuard, value, stage, sourceText, logProgress]()
            {
                if (!uiGuard || !uiGuard->m_binaryLogLoading)
                    return;
                uiGuard->m_progressBar->setValue(value);
                uiGuard->statusBar()->showMessage(
                    QString::fromUtf8(u8"正在解析 %1：%2").arg(stage, sourceText));
                if (logProgress)
                {
                    uiGuard->logFileTrace(
                        QStringLiteral("ZIP log progress stage=%1 value=%2 source=\"%3\"")
                            .arg(stage).arg(value).arg(sourceText));
                }
            }, Qt::QueuedConnection);
    };

    viewer::logparse::RbtLogParseOptions rbtOptions;
    rbtOptions.isCancelled = [cancelled]()
    {
        return cancelled->load(std::memory_order_relaxed);
    };
    rbtOptions.progress = [rbtSizes, postProgress](
        const std::filesystem::path& source,
        uint64_t processedBytes,
        uint64_t,
        size_t inputIndex,
        size_t)
    {
        uint64_t completed = 0;
        for (size_t index = 0; index < inputIndex && index < rbtSizes->size(); ++index)
            completed += (*rbtSizes)[index];
        completed += processedBytes;
        postProgress(completed, QStringLiteral("RBT"),
                     QString::fromStdWString(source.wstring()));
    };

    viewer::logparse::ParseOptions options;
    options.isCancelled = [cancelled]()
    {
        return cancelled->load(std::memory_order_relaxed);
    };
    options.progress = [hiklogSizes, rbtBytes, postProgress](
        const std::filesystem::path& source,
        uint64_t processedBytes,
        uint64_t,
        size_t inputIndex,
        size_t)
    {
        uint64_t completed = rbtBytes;
        for (size_t index = 0; index < inputIndex && index < hiklogSizes->size(); ++index)
            completed += (*hiklogSizes)[index];
        completed += processedBytes;
        postProgress(completed, QStringLiteral("HikLog"),
                     QString::fromStdWString(source.wstring()));
    };

    logFileTrace(QStringLiteral(
        "ZIP log parse dispatch archive=\"%1\" hiklogEntries=%2 rbtEntries=%3 bytes=%4")
        .arg(selectedArchive).arg(hiklogIndices.size())
        .arg(rbtIndices.size()).arg(totalBytes));
    logFileTrace(QString("ZIP log status progress shown visible=%1 value=%2 range=%3..%4")
                 .arg(m_progressBar->isVisible())
                 .arg(m_progressBar->value())
                 .arg(m_progressBar->minimum())
                 .arg(m_progressBar->maximum()));
    for (size_t order = 0; order < hiklogIndices.size(); ++order)
    {
        logFileTrace(QString("ZIP hiklog parse input order=%1 entryIndex=%2 bytes=%3")
                     .arg(order)
                     .arg(hiklogIndices[order])
                     .arg((*hiklogSizes)[order]));
    }
    for (size_t order = 0; order < rbtIndices.size(); ++order)
    {
        logFileTrace(QStringLiteral("ZIP RBT parse input order=%1 entryIndex=%2 bytes=%3")
                     .arg(order).arg(rbtIndices[order]).arg((*rbtSizes)[order]));
    }

    auto* watcher = new QFutureWatcher<ZipLogLoadResult>(this);
    connect(watcher, &QFutureWatcher<ZipLogLoadResult>::finished, this,
        [this, watcher, cancelled, selectedArchive]()
        {
            m_binaryLogLoading = false;
            ZipLogLoadResult loadResult = watcher->future().takeResult();
            watcher->deleteLater();
            const bool cancelRequested = cancelled->load(std::memory_order_relaxed);
            m_binaryLogCancel.reset();
            const bool cancelledResult = loadResult.cancelled
                || loadResult.rbt.cancelled
                || (loadResult.hasHiklog && loadResult.hiklog.cancelled);
            m_progressBar->setValue(cancelledResult ? 0 : 1000);

            QStringList rbtFiles;
            QStringList rbtErrors;
            for (const auto& file : loadResult.rbt.files)
            {
                const QString source = QString::fromStdWString(file.sourcePath.wstring());
                if (file.success())
                {
                    rbtFiles.push_back(QString::fromStdWString(file.outputPath.wstring()));
                    logFileTrace(QStringLiteral(
                        "ZIP RBT parse complete source=\"%1\" output=\"%2\" blocks=%3 bytes=%4")
                        .arg(source, rbtFiles.back())
                        .arg(file.blockCount).arg(file.outputBytes));
                }
                else if (!loadResult.rbt.cancelled)
                {
                    rbtErrors.push_back(QStringLiteral("%1：%2")
                        .arg(source, QString::fromUtf8(file.error.c_str())));
                    logFileTrace(QStringLiteral("ZIP RBT parse failed source=\"%1\" error=\"%2\"")
                        .arg(source, QString::fromUtf8(file.error.c_str())));
                }
            }
            if (!loadResult.rbt.archiveError.empty())
                rbtErrors.push_back(QString::fromUtf8(loadResult.rbt.archiveError.c_str()));
            if (!rbtFiles.isEmpty())
            {
                if (!m_rbtLogViewer)
                    m_rbtLogViewer = new RbtLogViewerWindow(this);
                m_rbtLogViewer->openFiles(rbtFiles);
            }

            logFileTrace(QString("ZIP log worker finished archive=\"%1\" "
                                 "cancelRequested=%2 parserCancelled=%3 shuttingDown=%4 "
                                 "hiklog=%5 hikSuccess=%6 rbtSuccess=%7 rbtErrors=%8")
                         .arg(selectedArchive)
                         .arg(cancelRequested)
                         .arg(cancelledResult)
                         .arg(m_isShuttingDown)
                         .arg(loadResult.hasHiklog)
                         .arg(loadResult.hasHiklog && loadResult.hiklog.success())
                         .arg(rbtFiles.size()).arg(rbtErrors.size()));

            if (cancelledResult)
            {
                m_binaryLogProgressActive = false;
                ++m_binaryLogProgressGeneration;
                m_progressBar->hide();
                m_progressBar->setFormat(QStringLiteral("%p%"));
                statusBar()->showMessage(QString::fromUtf8(u8"ZIP 日志解析已取消。"), 4000);
                return;
            }

            if (!loadResult.hasHiklog)
            {
                m_binaryLogProgressActive = false;
                ++m_binaryLogProgressGeneration;
                m_progressBar->hide();
                m_progressBar->setFormat(QStringLiteral("%p%"));
                statusBar()->showMessage(
                    QString::fromUtf8(u8"RBT 日志解析完成：成功 %1 个，失败 %2 个。")
                        .arg(rbtFiles.size()).arg(rbtErrors.size()), 8000);
                if (!rbtErrors.isEmpty())
                {
                    QMessageBox::warning(
                        this, QString::fromUtf8(u8"部分 RBT 日志解析失败"),
                        rbtErrors.join(QLatin1Char('\n')));
                }
                return;
            }

            viewer::logparse::ParseResult result = std::move(loadResult.hiklog);
            const size_t diagnosticLogCount = std::min<size_t>(result.diagnostics.size(), 20);
            for (size_t index = 0; index < diagnosticLogCount; ++index)
            {
                const auto& diagnostic = result.diagnostics[index];
                logFileTrace(QString("ZIP hiklog diagnostic index=%1 severity=%2 source=\"%3\" offset=%4 message=\"%5\"")
                             .arg(index)
                             .arg(diagnostic.severity == viewer::logparse::DiagnosticSeverity::Error
                                      ? QStringLiteral("error") : QStringLiteral("warning"))
                             .arg(QString::fromStdWString(diagnostic.filePath.wstring()))
                             .arg(diagnostic.byteOffset)
                             .arg(QString::fromUtf8(diagnostic.message.c_str())));
            }
            if (result.diagnostics.size() > diagnosticLogCount)
            {
                logFileTrace(QString("ZIP hiklog diagnostics truncated logged=%1 total=%2")
                             .arg(diagnosticLogCount).arg(result.diagnostics.size()));
            }

            if (cancelRequested)
            {
                logFileTrace(QString("ZIP hiklog cancellation request arrived after parsing completed; "
                                     "result will be kept archive=\"%1\"")
                             .arg(selectedArchive));
            }

            size_t warningCount = 0;
            for (const auto& diagnostic : result.diagnostics)
            {
                if (diagnostic.severity == viewer::logparse::DiagnosticSeverity::Warning)
                    ++warningCount;
            }
            if (!result.success())
            {
                m_binaryLogProgressActive = false;
                ++m_binaryLogProgressGeneration;
                m_progressBar->hide();
                m_progressBar->setFormat(QStringLiteral("%p%"));
                statusBar()->showMessage(QString::fromUtf8(u8"HikLog 解析失败。"), 5000);
                logFileTrace(QString("ZIP hiklog parse failed archive=\"%1\" diagnostics=%2")
                             .arg(selectedArchive).arg(result.diagnostics.size()));
                QMessageBox::critical(
                    this,
                    QString::fromUtf8(u8"HikLog 解析失败"),
                    parseDiagnosticsText(result));
                if (!rbtErrors.isEmpty())
                {
                    QMessageBox::warning(
                        this, QString::fromUtf8(u8"部分 RBT 日志解析失败"),
                        rbtErrors.join(QLatin1Char('\n')));
                }
                return;
            }

            const size_t rows = result.timestampCount;
            const size_t columns = result.columns.size();
            statusBar()->showMessage(QString::fromUtf8(u8"正在将 HikLog 解析结果载入数据管理器…"));
            if (!m_viewer.AdoptBinaryLog(
                    std::move(result), selectedArchive.toUtf8().toStdString()))
            {
                m_binaryLogProgressActive = false;
                ++m_binaryLogProgressGeneration;
                m_progressBar->hide();
                m_progressBar->setFormat(QStringLiteral("%p%"));
                statusBar()->showMessage(QString::fromUtf8(u8"HikLog 载入失败。"), 5000);
                QMessageBox::critical(
                    this,
                    QString::fromUtf8(u8"HikLog 载入失败"),
                    QString::fromUtf8(m_viewer.GetLastError().c_str()));
                if (!rbtErrors.isEmpty())
                {
                    QMessageBox::warning(
                        this, QString::fromUtf8(u8"部分 RBT 日志解析失败"),
                        rbtErrors.join(QLatin1Char('\n')));
                }
                return;
            }
            statusBar()->showMessage(
                QString::fromUtf8(u8"ZIP 日志载入完成：HikLog %1 列、%2 行、%3 条警告；RBT 成功 %4 个。")
                    .arg(columns).arg(rows).arg(warningCount).arg(rbtFiles.size()),
                8000);
            logFileTrace(QString("ZIP hiklog parse complete archive=\"%1\" columns=%2 rows=%3 warnings=%4")
                         .arg(selectedArchive).arg(columns).arg(rows).arg(warningCount));
            if (!rbtErrors.isEmpty())
            {
                QMessageBox::warning(
                    this, QString::fromUtf8(u8"部分 RBT 日志解析失败"),
                    rbtErrors.join(QLatin1Char('\n')));
            }
        });

    const std::filesystem::path rbtOutputDirectory(
        rbtLogDirectory().toStdWString());
    watcher->setFuture(QtConcurrent::run(
        [archivePath, hiklogIndices, rbtIndices, rbtOutputDirectory,
         options, rbtOptions, cancelled]() mutable
        {
            ZipLogLoadResult result;
            result.hasHiklog = !hiklogIndices.empty();
            if (!rbtIndices.empty())
            {
                result.rbt = viewer::Viewer::ParseRbtZipEntries(
                    archivePath, rbtIndices, rbtOutputDirectory, rbtOptions);
            }
            if (!cancelled->load(std::memory_order_relaxed)
                && !hiklogIndices.empty())
            {
                result.hiklog = viewer::Viewer::ParseZipEntries(
                    archivePath, hiklogIndices, options);
            }
            else if (!hiklogIndices.empty())
            {
                result.cancelled = true;
            }
            return result;
        }));
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
