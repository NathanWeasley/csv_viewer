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
#include <qfileinfo.h>
#include <qjsondocument.h>
#include <qjsonarray.h>
#include <qjsonobject.h>

// ============================================================
// Alias: 自动重命名
// ============================================================

void UI::loadAliasFile()
{
    QString dir = QCoreApplication::applicationDirPath();
    QDir().mkpath(dir + "/user");
    QString path = dir + "/user/alias.json";
    logFileTrace(QString("alias load enter path=\"%1\" exists=%2")
                 .arg(path).arg(QFile::exists(path)));

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        logFileTrace("alias load skipped: file unavailable");
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    m_aliasMap.clear();
    if (!doc.isObject())
    {
        logFileTrace("alias load aborted: JSON root is not an object");
        return;
    }

    QJsonArray arr = doc.object().value("aliases").toArray();
    for (const auto& item : arr)
    {
        QJsonObject obj = item.toObject();
        std::string from = obj.value("from").toString().toStdString();
        std::string to   = obj.value("to").toString().toStdString();
        if (!from.empty() && !to.empty())
            m_aliasMap[from] = to;
    }

    m_viewer.GetDataManager().SetAliasMap(m_aliasMap);
    logFileTrace(QString("alias load leave entries=%1").arg(m_aliasMap.size()));
}

void UI::saveAliasFile()
{
    QString dir = QCoreApplication::applicationDirPath();
    QDir().mkpath(dir + "/user");
    QString path = dir + "/user/alias.json";
    logFileTrace(QString("alias save enter path=\"%1\" entries=%2")
                 .arg(path).arg(m_aliasMap.size()));

    QJsonArray arr;
    for (const auto& [from, to] : m_aliasMap)
    {
        QJsonObject obj;
        obj["from"] = QString::fromStdString(from);
        obj["to"]   = QString::fromStdString(to);
        arr.append(obj);
    }

    QJsonObject root;
    root["aliases"] = arr;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        file.close();
    }

    m_viewer.GetDataManager().SetAliasMap(m_aliasMap);
    logFileTrace(QString("alias save leave path=\"%1\" exists=%2 size=%3")
                 .arg(path).arg(QFile::exists(path)).arg(QFileInfo(path).size()));
}

void UI::showAliasDialog()
{
    logOperationTrace(QString("alias dialog enter aliases=%1 columns=%2")
                      .arg(m_aliasMap.size()).arg(m_viewer.GetDataManager().GetColumnCount()));
    AliasDialog dlg(this);
    dlg.setAliases(m_aliasMap);

    // 传入已加载的原始列名列表（用于防重名校验）
    const auto& rawNames = m_viewer.GetDataManager().GetRawColumnNames();
    if (!rawNames.empty())
    {
        std::vector<std::string> rawVec(rawNames.begin(), rawNames.end());
        dlg.setExistingNames(rawVec);
    }

    if (dlg.exec() != QDialog::Accepted)
    {
        logOperationTrace("alias dialog cancelled");
        return;
    }

    m_aliasMap = dlg.getAliases();
    saveAliasFile();
    logOperationTrace(QString("alias dialog accepted aliases=%1").arg(m_aliasMap.size()));
}
