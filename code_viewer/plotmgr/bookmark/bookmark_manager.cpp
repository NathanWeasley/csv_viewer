#include "bookmark_manager.h"
#include "code_viewer/base/trace_logger.h"
#include "code_viewer/jsonmgr/highlight_rule_json.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace viewer
{

// ============================================================
// 内部辅助：收集所有名称
// ============================================================
void BookmarkMgr::collectAllNames(const BookmarkFolder& folder, std::vector<std::string>& out) const
{
    out.push_back(folder.name);
    for (const auto& sub : folder.subFolders)
        collectAllNames(sub, out);
    for (const auto& e : folder.entries)
        out.push_back(e.name);
}

// ============================================================
// 内部辅助：查找文件夹（非 const）
// ============================================================
BookmarkFolder* BookmarkMgr::findFolder(const std::string& path)
{
    return const_cast<BookmarkFolder*>(
        const_cast<const BookmarkMgr*>(this)->findFolder(path));
}

// ============================================================
// 内部辅助：查找文件夹（const）
// ============================================================
const BookmarkFolder* BookmarkMgr::findFolder(const std::string& path) const
{
    if (path.empty())
        return &m_root;

    const BookmarkFolder* cur = &m_root;
    std::string remaining = path;
    size_t pos = 0;

    while (!remaining.empty())
    {
        pos = remaining.find('/');
        std::string seg = (pos == std::string::npos)
            ? remaining : remaining.substr(0, pos);

        bool found = false;
        for (const auto& sub : cur->subFolders)
        {
            if (sub.name == seg)
            {
                cur = &sub;
                found = true;
                break;
            }
        }

        if (!found)
            return nullptr;

        if (pos == std::string::npos)
            break;
        remaining = remaining.substr(pos + 1);
    }

    return cur;
}

// ============================================================
// 全局名称唯一性检查
// ============================================================
bool BookmarkMgr::isNameUnique(const std::string& name) const
{
    std::vector<std::string> allNames;
    for (const auto& sub : m_root.subFolders)
        collectAllNames(sub, allNames);
    for (const auto& e : m_root.entries)
        allNames.push_back(e.name);

    for (const auto& n : allNames)
    {
        if (n == name)
            return false;
    }
    return true;
}

// ============================================================
// 文件夹 CRUD
// ============================================================

bool BookmarkMgr::addFolder(const std::string& parentPath, const std::string& name)
{
    trace::write(trace::Category::Bookmark,
                 QString("manager add folder enter parent=\"%1\" name=\"%2\"")
                     .arg(QString::fromStdString(parentPath), QString::fromStdString(name)));
    if (name.empty())
        return false;

    if (!isNameUnique(name))
        return false;

    BookmarkFolder* parent = findFolder(parentPath);
    if (!parent)
        return false;

    for (const auto& sub : parent->subFolders)
    {
        if (sub.name == name)
            return false;
    }

    BookmarkFolder newFolder;
    newFolder.name = name;
    parent->subFolders.push_back(std::move(newFolder));
    trace::write(trace::Category::Bookmark, "manager add folder leave result=success");
    return true;
}

bool BookmarkMgr::removeFolder(const std::string& path)
{
    trace::write(trace::Category::Bookmark,
                 QString("manager remove folder enter path=\"%1\"").arg(QString::fromStdString(path)));
    if (path.empty())
        return false;

    size_t lastSlash = path.rfind('/');
    std::string parentPath = (lastSlash == std::string::npos)
        ? "" : path.substr(0, lastSlash);
    std::string folderName = (lastSlash == std::string::npos)
        ? path : path.substr(lastSlash + 1);

    BookmarkFolder* parent = findFolder(parentPath);
    if (!parent)
        return false;

    for (auto it = parent->subFolders.begin(); it != parent->subFolders.end(); ++it)
    {
        if (it->name == folderName)
        {
            parent->subFolders.erase(it);
            trace::write(trace::Category::Bookmark, "manager remove folder leave result=success");
            return true;
        }
    }
    return false;
}

bool BookmarkMgr::renameFolder(const std::string& path, const std::string& newName)
{
    trace::write(trace::Category::Bookmark,
                 QString("manager rename folder enter path=\"%1\" newName=\"%2\"")
                     .arg(QString::fromStdString(path), QString::fromStdString(newName)));
    if (path.empty() || newName.empty())
        return false;

    if (!isNameUnique(newName))
        return false;

    size_t lastSlash = path.rfind('/');
    std::string parentPath = (lastSlash == std::string::npos)
        ? "" : path.substr(0, lastSlash);
    std::string folderName = (lastSlash == std::string::npos)
        ? path : path.substr(lastSlash + 1);

    BookmarkFolder* parent = findFolder(parentPath);
    if (!parent)
        return false;

    for (const auto& sub : parent->subFolders)
    {
        if (sub.name == newName)
            return false;
    }

    for (auto& sub : parent->subFolders)
    {
        if (sub.name == folderName)
        {
            sub.name = newName;
            trace::write(trace::Category::Bookmark, "manager rename folder leave result=success");
            return true;
        }
    }
    return false;
}

bool BookmarkMgr::moveFolder(const std::string& fromPath, const std::string& toParentPath)
{
    trace::write(trace::Category::Bookmark,
                 QString("manager move folder enter from=\"%1\" to=\"%2\"")
                     .arg(QString::fromStdString(fromPath), QString::fromStdString(toParentPath)));
    if (fromPath.empty())
        return false;

    if (toParentPath.find(fromPath + "/") == 0 || toParentPath == fromPath)
        return false;

    size_t lastSlash = fromPath.rfind('/');
    std::string fromParentPath = (lastSlash == std::string::npos)
        ? "" : fromPath.substr(0, lastSlash);
    std::string folderName = (lastSlash == std::string::npos)
        ? fromPath : fromPath.substr(lastSlash + 1);

    BookmarkFolder* fromParent = findFolder(fromParentPath);
    if (!fromParent)
        return false;

    BookmarkFolder* toParent = findFolder(toParentPath);
    if (!toParent)
        return false;

    for (const auto& sub : toParent->subFolders)
    {
        if (sub.name == folderName)
            return false;
    }

    BookmarkFolder movingFolder;
    for (auto it = fromParent->subFolders.begin();
         it != fromParent->subFolders.end(); ++it)
    {
        if (it->name == folderName)
        {
            movingFolder = std::move(*it);
            fromParent->subFolders.erase(it);
            break;
        }
    }

    if (movingFolder.name.empty())
        return false;

    toParent->subFolders.push_back(std::move(movingFolder));
    trace::write(trace::Category::Bookmark, "manager move folder leave result=success");
    return true;
}


// ============================================================
// 收藏项 CRUD
// ============================================================

bool BookmarkMgr::addBookmark(const std::string& folderPath, const BookmarkEntry& entry)
{
    trace::write(trace::Category::Bookmark,
                 QString("manager add bookmark enter folder=\"%1\" name=\"%2\" dataItems=%3 graphs=%4")
                     .arg(QString::fromStdString(folderPath), QString::fromStdString(entry.name))
                     .arg(entry.dataItems.size()).arg(entry.graphs.size()));
    if (entry.name.empty())
        return false;

    if (!isNameUnique(entry.name))
        return false;

    BookmarkFolder* folder = findFolder(folderPath);
    if (!folder)
        return false;

    for (const auto& e : folder->entries)
    {
        if (e.name == entry.name)
            return false;
    }

    folder->entries.push_back(entry);
    trace::write(trace::Category::Bookmark, "manager add bookmark leave result=success");
    return true;
}

bool BookmarkMgr::removeBookmark(const std::string& folderPath, const std::string& name)
{
    trace::write(trace::Category::Bookmark,
                 QString("manager remove bookmark enter folder=\"%1\" name=\"%2\"")
                     .arg(QString::fromStdString(folderPath), QString::fromStdString(name)));
    BookmarkFolder* folder = findFolder(folderPath);
    if (!folder)
        return false;

    for (auto it = folder->entries.begin(); it != folder->entries.end(); ++it)
    {
        if (it->name == name)
        {
            folder->entries.erase(it);
            trace::write(trace::Category::Bookmark, "manager remove bookmark leave result=success");
            return true;
        }
    }
    return false;
}

bool BookmarkMgr::renameBookmark(const std::string& folderPath,
                                  const std::string& oldName,
                                  const std::string& newName)
{
    trace::write(trace::Category::Bookmark,
                 QString("manager rename bookmark enter folder=\"%1\" old=\"%2\" new=\"%3\"")
                     .arg(QString::fromStdString(folderPath), QString::fromStdString(oldName),
                          QString::fromStdString(newName)));
    if (newName.empty())
        return false;

    BookmarkFolder* folder = findFolder(folderPath);
    if (!folder)
        return false;

    // 全局检查（排除自身）
    for (const auto& e : folder->entries)
    {
        if (e.name != oldName && e.name == newName)
            return false;
    }

    // 遍历所有文件夹检查
    std::vector<std::string> allNames;
    for (const auto& sub : m_root.subFolders)
        collectAllNames(sub, allNames);
    for (const auto& e : m_root.entries)
    {
        if (e.name != oldName)
            allNames.push_back(e.name);
    }

    for (const auto& n : allNames)
    {
        if (n == newName)
            return false;
    }

    for (auto& e : folder->entries)
    {
        if (e.name == oldName)
        {
            e.name = newName;
            trace::write(trace::Category::Bookmark, "manager rename bookmark leave result=success");
            return true;
        }
    }
    return false;
}

bool BookmarkMgr::moveBookmark(const std::string& fromPath,
                                const std::string& toPath,
                                const std::string& name)
{
    trace::write(trace::Category::Bookmark,
                 QString("manager move bookmark enter from=\"%1\" to=\"%2\" name=\"%3\"")
                     .arg(QString::fromStdString(fromPath), QString::fromStdString(toPath),
                          QString::fromStdString(name)));
    if (fromPath == toPath)
        return false;

    BookmarkFolder* fromFolder = findFolder(fromPath);
    if (!fromFolder)
        return false;

    BookmarkFolder* toFolder = findFolder(toPath);
    if (!toFolder)
        return false;

    for (const auto& e : toFolder->entries)
    {
        if (e.name == name)
            return false;
    }

    BookmarkEntry movingEntry;
    for (auto it = fromFolder->entries.begin(); it != fromFolder->entries.end(); ++it)
    {
        if (it->name == name)
        {
            movingEntry = std::move(*it);
            fromFolder->entries.erase(it);
            break;
        }
    }

    if (movingEntry.name.empty())
        return false;

    toFolder->entries.push_back(std::move(movingEntry));
    trace::write(trace::Category::Bookmark, "manager move bookmark leave result=success");
    return true;
}

// ============================================================
// 查询
// ============================================================

const BookmarkEntry* BookmarkMgr::find(const std::string& folderPath,
                                         const std::string& name) const
{
    const BookmarkFolder* folder = findFolder(folderPath);
    if (!folder)
        return nullptr;

    for (const auto& e : folder->entries)
    {
        if (e.name == name)
            return &e;
    }
    return nullptr;
}


// ============================================================
// JSON 序列化 / 反序列化
// ============================================================

void BookmarkMgr::graphToJson(const GraphStyleSnapshot& gs, QJsonObject& out) const
{
    out["name"]           = QString::fromStdString(gs.dataItemName);
    out["lineStyle"]      = gs.lineStyle;
    out["penStyle"]       = gs.penStyle;
    out["penWidth"]       = gs.penWidth;
    out["penColor"]       = QString::fromStdString(gs.penColor);
    out["scatterShape"]   = gs.scatterShape;
    out["scatterSize"]    = gs.scatterSize;
    out["scatterColor"]   = QString::fromStdString(gs.scatterColor);
    out["expressionText"] = QString::fromStdString(gs.expressionText);
    out["isEdited"]       = gs.isEdited;
}

void BookmarkMgr::jsonToGraph(const QJsonObject& obj, GraphStyleSnapshot& out)
{
    out.dataItemName  = obj.value("name").toString().toStdString();
    out.lineStyle     = obj.value("lineStyle").toInt(1);
    out.penStyle      = obj.value("penStyle").toInt(1);
    out.penWidth      = obj.value("penWidth").toInt(1);
    out.penColor      = obj.value("penColor").toString("#0072bd").toStdString();
    out.scatterShape  = obj.value("scatterShape").toInt(0);
    out.scatterSize   = obj.value("scatterSize").toInt(6);
    out.scatterColor  = obj.value("scatterColor").toString("#000000").toStdString();
    out.expressionText = obj.value("expressionText").toString().toStdString();
    out.isEdited      = obj.value("isEdited").toBool(false);
}

void BookmarkMgr::entryToJson(const BookmarkEntry& entry, QJsonObject& out) const
{
    out["name"]          = QString::fromStdString(entry.name);
    out["xAxisColumn"]   = static_cast<qint64>(entry.xAxisColumn);
    out["useIndexXAxis"] = entry.useIndexXAxis;
    out["legendVisible"] = entry.legendVisible;
    out["logX"]          = entry.logX;
    out["logY"]          = entry.logY;

    QJsonArray diArr;
    for (const auto& di : entry.dataItems)
        diArr.append(QString::fromStdString(di));
    out["dataItems"] = diArr;

    QJsonArray graphArr;
    for (const auto& gs : entry.graphs)
    {
        QJsonObject gsObj;
        graphToJson(gs, gsObj);
        graphArr.append(gsObj);
    }
    out["graphs"] = graphArr;

    QJsonArray hlArr;
    for (const auto& rule : entry.highlights)
        hlArr.append(HighlightRuleJson::toJson(rule));
    out["highlights"] = hlArr;
}

void BookmarkMgr::jsonToEntry(const QJsonObject& obj, BookmarkEntry& out)
{
    out.name          = obj.value("name").toString().toStdString();
    out.xAxisColumn   = static_cast<size_t>(obj.value("xAxisColumn").toInt(-1));
    out.useIndexXAxis = obj.contains("useIndexXAxis")
        ? obj.value("useIndexXAxis").toBool(false)
        : out.xAxisColumn == static_cast<size_t>(-1);
    out.legendVisible = obj.value("legendVisible").toBool(false);
    out.logX          = obj.value("logX").toBool(false);
    out.logY          = obj.value("logY").toBool(false);

    out.dataItems.clear();
    QJsonArray diArr = obj.value("dataItems").toArray();
    for (const auto& val : diArr)
        out.dataItems.push_back(val.toString().toStdString());

    out.graphs.clear();
    QJsonArray graphArr = obj.value("graphs").toArray();
    for (const auto& gval : graphArr)
    {
        GraphStyleSnapshot gs;
        jsonToGraph(gval.toObject(), gs);
        out.graphs.push_back(std::move(gs));
    }

    out.highlights.clear();
    QJsonArray hlArr = obj.value("highlights").toArray();
    for (const auto& hval : hlArr)
    {
        if (!hval.isObject())
            continue;
        HighlightRule rule;
        if (HighlightRuleJson::fromJson(hval.toObject(), &rule))
            out.highlights.push_back(std::move(rule));
    }
}

void BookmarkMgr::folderToJson(const BookmarkFolder& folder, QJsonObject& out) const
{
    out["name"] = QString::fromStdString(folder.name);

    QJsonArray subFolderArr;
    for (const auto& sub : folder.subFolders)
    {
        QJsonObject subObj;
        folderToJson(sub, subObj);
        subFolderArr.append(subObj);
    }
    out["folders"] = subFolderArr;

    QJsonArray entryArr;
    for (const auto& e : folder.entries)
    {
        QJsonObject eObj;
        entryToJson(e, eObj);
        entryArr.append(eObj);
    }
    out["bookmarks"] = entryArr;
}

void BookmarkMgr::jsonToFolder(const QJsonObject& obj, BookmarkFolder& out)
{
    out.name = obj.value("name").toString().toStdString();

    out.subFolders.clear();
    QJsonArray subFolderArr = obj.value("folders").toArray();
    for (const auto& sval : subFolderArr)
    {
        BookmarkFolder sub;
        jsonToFolder(sval.toObject(), sub);
        out.subFolders.push_back(std::move(sub));
    }

    out.entries.clear();
    QJsonArray entryArr = obj.value("bookmarks").toArray();
    for (const auto& eval : entryArr)
    {
        BookmarkEntry entry;
        jsonToEntry(eval.toObject(), entry);
        out.entries.push_back(std::move(entry));
    }
}


// ============================================================
// 文件 I/O
// ============================================================

void BookmarkMgr::loadFromFile(const std::string& path)
{
    trace::write(trace::Category::Bookmark,
                 QString("manager load file enter path=\"%1\"").arg(QString::fromStdString(path)));
    clear();

    QString qPath = QString::fromStdString(path);
    QFile file(qPath);
    if (!file.open(QIODevice::ReadOnly))
        return;

    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return;

    QJsonObject rootObj = doc.object();

    // 支持旧版格式（扁平数组 "bookmarks"）和新版格式（"root"）
    if (rootObj.contains("root"))
    {
        QJsonObject rObj = rootObj.value("root").toObject();
        jsonToFolder(rObj, m_root);
    }
    else if (rootObj.contains("bookmarks"))
    {
        // 旧版扁平数组格式 — 将所有书签放在 root 层
        QJsonArray arr = rootObj.value("bookmarks").toArray();
        for (const auto& val : arr)
        {
            BookmarkEntry entry;
            jsonToEntry(val.toObject(), entry);
            m_root.entries.push_back(std::move(entry));
        }
    }
    trace::write(trace::Category::Bookmark,
                 QString("manager load file leave rootFolders=%1 rootEntries=%2")
                     .arg(m_root.subFolders.size()).arg(m_root.entries.size()));
}

void BookmarkMgr::saveToFile(const std::string& path)
{
    trace::write(trace::Category::Bookmark,
                 QString("manager save file enter path=\"%1\" rootFolders=%2 rootEntries=%3")
                     .arg(QString::fromStdString(path)).arg(m_root.subFolders.size()).arg(m_root.entries.size()));
    QString qPath = QString::fromStdString(path);

    // 确保目录存在
    QDir().mkpath(QFileInfo(qPath).absolutePath());

    QJsonObject rootObj;
    rootObj["version"] = 1;

    QJsonObject rObj;
    folderToJson(m_root, rObj);
    rootObj["root"] = rObj;

    QJsonDocument doc(rootObj);
    QFile file(qPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        file.write(doc.toJson(QJsonDocument::Indented));
        file.close();
    }
    trace::write(trace::Category::Bookmark,
                 QString("manager save file leave path=\"%1\" exists=%2 size=%3")
                     .arg(qPath).arg(QFile::exists(qPath)).arg(QFileInfo(qPath).size()));
}

} // namespace viewer

