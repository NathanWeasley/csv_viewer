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

// ============================================================
// Alias: 自动重命名
// ============================================================

void UI::loadAliasFile()
{
    QString dir = QCoreApplication::applicationDirPath();
    QDir().mkpath(dir + "/user");
    QString path = dir + "/user/alias.json";

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();

    m_aliasMap.clear();
    if (!doc.isObject())
        return;

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
}

void UI::saveAliasFile()
{
    QString dir = QCoreApplication::applicationDirPath();
    QDir().mkpath(dir + "/user");
    QString path = dir + "/user/alias.json";

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
}

void UI::showAliasDialog()
{
    AliasDialog dlg(this);
    dlg.setAliases(m_aliasMap);

    if (dlg.exec() != QDialog::Accepted)
        return;

    m_aliasMap = dlg.getAliases();
    saveAliasFile();
}
