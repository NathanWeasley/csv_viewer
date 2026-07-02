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

extern bool isSystemInDark();

UI::UI(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    m_dockManager = new ads::CDockManager(ui.centralWidget);

    /** 先读取 Settings 状态（init/createMenu 中需要用到） */
    {
        QString configPath = QCoreApplication::applicationDirPath() + "/user/config.ini";
        QSettings settings(configPath, QSettings::IniFormat);

        // 读取自适应降采样设置（默认 true）
        m_adaptiveDownsampling = settings.value("adaptiveDownsampling", true).toBool();
        viewer::QCPColumnGraph::s_adaptiveSamplingEnabled = m_adaptiveDownsampling;

        // 读取 OpenGL 设置（默认 true）
        m_openglEnabled = settings.value("openglEnabled", true).toBool();

        // 读取抗锯齿设置（默认 false）
        m_antiAliasingEnabled = settings.value("antiAliasing", false).toBool();
        viewer::QCPColumnGraph::s_antiAliasingEnabled = m_antiAliasingEnabled;
    }

    init();

    /** Restore window geometry & docking state */
    {
        QString configPath = QCoreApplication::applicationDirPath() + "/user/config.ini";
        QSettings settings(configPath, QSettings::IniFormat);

        if (settings.contains("geometry"))
        {
            restoreGeometry(settings.value("geometry").toByteArray());
        }

        if (settings.contains("dockingState"))
        {
            m_dockManager->restoreState(settings.value("dockingState").toByteArray());
        }
    }
}

UI::~UI()
{
    saveState();
}

void UI::init()
{
    setCentralWidget(m_dockManager);

    // ---- 初始化样式管理器 ----
    {
        auto& sm = m_viewer.GetStyleManager();
        bool dark = isSystemInDark();

        // 尝试从 user/style.json 加载
        QString stylePath = QCoreApplication::applicationDirPath() + "/user/style.json";
        if (!sm.load(stylePath.toStdString()))
        {
            sm.initializeDefaults(dark);
        }
    }

    createMenu();
    createMain();

    // ---- 加载 X 轴规则 ----
    {
        QString jsonPath = QCoreApplication::applicationDirPath() + "/user/xaxis.json";
        m_viewer.GetDataManager().LoadXAxisRules(jsonPath.toStdString());
    }

    // ---- 加载书签 ----
    loadBookmarkFile();

    createToolbar();
    createStatusbar();

    bindCursorManagerCallbacks();
}

void UI::saveState()
{
    auto& sm = m_viewer.GetStyleManager();
    QString stylePath = QCoreApplication::applicationDirPath() + "/user/style.json";
    sm.save(stylePath.toStdString());
}

void UI::closeEvent(QCloseEvent* event)
{
    QString configPath = QCoreApplication::applicationDirPath() + "/user/config.ini";
    QDir().mkpath(QCoreApplication::applicationDirPath() + "/user");
    QSettings settings(configPath, QSettings::IniFormat);

    settings.setValue("geometry", saveGeometry());
    if (m_dockManager)
    {
        settings.setValue("dockingState", m_dockManager->saveState());
    }

    settings.setValue("adaptiveDownsampling", m_adaptiveDownsampling);
    settings.setValue("openglEnabled", m_openglEnabled);
    settings.setValue("antiAliasing", m_antiAliasingEnabled);

    QMainWindow::closeEvent(event);
}
// ============================================================
// 双击数据树项 → 添加到激活图窗
// ============================================================

void UI::onDataItemDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
    if (!item)
        return;

    std::string yColName = item->text(0).toStdString();

    auto& dm = m_viewer.GetDataManager();
    auto& pm = m_viewer.GetPlotManager();

    // 验证列存在
    size_t yIdx = dm.GetColumnIndex(yColName);
    if (yIdx == static_cast<size_t>(-1))
        return;

    // 确保有激活页面（无激活页时自动创建）
    if (!pm.hasActivePage())
        pm.addPage();

    // ---- X 轴自动/手动设置 ----
    int pageIdx = pm.activePageIndex();
    size_t xIdx = pm.xAxisColumn(pageIdx);
    if (xIdx == static_cast<size_t>(-1))
    {
        // 1. 尝试使用全局检测结果
        size_t globalX = dm.GetXAxisColumn();
        if (globalX != static_cast<size_t>(-1))
        {
            pm.setXAxisColumn(pageIdx, globalX);
        }
        else
        {
            // 2. 全局也没检测到 → 弹出对话框让用户选择
            const auto& colNames = dm.GetColumnNames();
            QStringList items;
            items << QString::fromUtf8("[数据索引]");
            for (const auto& name : colNames)
                items << QString::fromStdString(name);

            bool ok = false;
            QString choice = QInputDialog::getItem(
                this,
                QString::fromUtf8("选择 X 轴"),
                QString::fromUtf8("未检测到默认 X 轴变量，请手动选择："),
                items, 0, false, &ok);

            if (!ok || choice.isEmpty())
                return;  // 用户取消

            if (choice == QString::fromUtf8("[数据索引]"))
            {
                pm.setXAxisColumn(pageIdx, static_cast<size_t>(-1));
            }
            else
            {
                size_t selIdx = dm.GetColumnIndex(choice.toStdString());
                pm.setXAxisColumn(pageIdx, selIdx);
            }
        }
    }

    // 检查：如果 yColName 是当前图窗的 X 轴列，跳过（不添加到 Y 轴）
    {
        size_t pxIdx = pm.xAxisColumn(pageIdx);
        if (pxIdx != static_cast<size_t>(-1) && pxIdx < dm.GetColumnNames().size())
        {
            const auto& colNames = dm.GetColumnNames();
            if (yColName == colNames[pxIdx])
                return;
        }
    }

    // FFT 图窗禁止添加数据项
    if (pm.hasActivePage() && pm.isFFTPage(pm.activePageIndex()))
        return;

    // 向 PlotManager 注册（自动处理去重）
    pm.addDataToActivePage(yColName);
}