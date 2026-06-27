#include "code_viewer/plotmgr/bookmark/bookmark_mgr.h"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace viewer
{

// ============================================================
// 简易 JSON 序列化/反序列化（无外部依赖）
// ============================================================

static void jsonEscape(std::ostream& os, const std::string& s)
{
    os << '"';
    for (char c : s)
    {
        if (c == '"' || c == '\\')
            os << '\\' << c;
        else
            os << c;
    }
    os << '"';
}

static std::string jsonUnescape(const std::string& s)
{
    std::string result;
    result.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            ++i;
            result += s[i];
        }
        else
        {
            result += s[i];
        }
    }
    return result;
}

static std::string readFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open())
        return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static bool writeFile(const std::string& path, const std::string& content)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f.is_open())
        return false;
    f << content;
    return true;
}

// ============================================================
// BookmarkMgr 实现
// ============================================================

bool BookmarkMgr::saveToFile(const std::string& filePath) const
{
    std::ostringstream ss;

    ss << "{\n  \"bookmarks\": [\n";

    for (size_t ei = 0; ei < m_entries.size(); ++ei)
    {
        const auto& entry = m_entries[ei];

        ss << "    {\n";
        ss << "      \"name\": "; jsonEscape(ss, entry.name); ss << ",\n";
        ss << "      \"xAxisColumn\": " << entry.xAxisColumn << ",\n";
        ss << "      \"legendVisible\": " << (entry.legendVisible ? "true" : "false") << ",\n";

        // dataItems
        ss << "      \"dataItems\": [";
        for (size_t di = 0; di < entry.dataItems.size(); ++di)
        {
            if (di > 0) ss << ", ";
            jsonEscape(ss, entry.dataItems[di]);
        }
        ss << "],\n";

        // graphs
        ss << "      \"graphs\": [\n";
        for (size_t gi = 0; gi < entry.graphs.size(); ++gi)
        {
            const auto& g = entry.graphs[gi];
            ss << "        {";
            ss << "\"name\": "; jsonEscape(ss, g.dataItemName); ss << ", ";
            ss << "\"penStyle\": " << g.penStyle << ", ";
            ss << "\"penWidth\": " << g.penWidth << ", ";
            ss << "\"penColor\": "; jsonEscape(ss, g.penColor); ss << ", ";
            ss << "\"scatterShape\": " << g.scatterShape << ", ";
            ss << "\"scatterSize\": " << g.scatterSize << ", ";
            ss << "\"scatterColor\": "; jsonEscape(ss, g.scatterColor); ss << ", ";
            ss << "\"expressionText\": "; jsonEscape(ss, g.expressionText); ss << ", ";
            ss << "\"isEdited\": " << (g.isEdited ? "true" : "false");
            ss << "}";
            if (gi + 1 < entry.graphs.size()) ss << ",";
            ss << "\n";
        }
        ss << "      ],\n";

        // highlights
        ss << "      \"highlights\": [\n";
        for (size_t hi = 0; hi < entry.highlights.size(); ++hi)
        {
            const auto& h = entry.highlights[hi];
            ss << "        {";
            ss << "\"dataColumn\": "; jsonEscape(ss, h.dataColumn); ss << ", ";
            ss << "\"condition\": " << static_cast<int>(h.condition) << ", ";
            ss << "\"value1\": " << h.value1 << ", ";
            ss << "\"value2\": " << h.value2 << ", ";
            ss << "\"color\": "; jsonEscape(ss, h.color.name().toStdString()); ss << ", ";
            ss << "\"alpha\": " << h.alpha << ", ";
            ss << "\"label\": "; jsonEscape(ss, h.label);
            ss << "}";
            if (hi + 1 < entry.highlights.size()) ss << ",";
            ss << "\n";
        }
        ss << "      ]\n";

        ss << "    }";
        if (ei + 1 < m_entries.size()) ss << ",";
        ss << "\n";
    }

    ss << "  ]\n}\n";

    return writeFile(filePath, ss.str());
}

// ---- 极简 JSON 解析器（仅支持上述结构）----
namespace {
    struct Token {
        enum { Null, Str, Int, Bool, LBrace, RBrace, LBracket, RBracket, Colon, Comma } type;
        std::string strVal;
        int64_t intVal = 0;
        bool boolVal = false;
    };

    struct Parser {
        const char* p;
        const char* end;

        void skipWS() {
            while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) ++p;
        }

        bool match(char c) {
            skipWS();
            if (p < end && *p == c) { ++p; return true; }
            return false;
        }

        std::string readString() {
            skipWS();
            if (p >= end || *p != '"') return {};
            ++p;
            std::string s;
            while (p < end && *p != '"') {
                if (*p == '\\' && p + 1 < end) { ++p; s += *p++; }
                else { s += *p++; }
            }
            if (p < end) ++p;
            return s;
        }

        int64_t readInt() {
            skipWS();
            bool neg = false;
            if (p < end && *p == '-') { neg = true; ++p; }
            int64_t v = 0;
            while (p < end && *p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
            return neg ? -v : v;
        }

        bool readBool() {
            skipWS();
            if (p + 3 < end && strncmp(p, "true", 4) == 0) { p += 4; return true; }
            if (p + 4 < end && strncmp(p, "false", 5) == 0) { p += 5; return false; }
            return false;
        }

        bool parseEntry(BookmarkEntry& entry);
        bool parseGraph(GraphStyleSnapshot& g);
        bool parseHighlight(HighlightRule& h);
    };
}

bool BookmarkMgr::loadFromFile(const std::string& filePath)
{
    std::string content = readFile(filePath);
    if (content.empty())
        return false;

    Parser par;
    par.p = content.c_str();
    par.end = par.p + content.size();

    // parse root { "bookmarks": [ ... ] }
    if (!par.match('{')) return false;
    par.readString(); // "bookmarks"
    if (!par.match(':')) return false;
    if (!par.match('[')) return false;

    m_entries.clear();

    bool first = true;
    while (!par.match(']')) {
        if (!first && !par.match(',')) return false;
        first = false;
        if (!par.match('{')) return false;

        BookmarkEntry entry;
        if (!par.parseEntry(entry)) return false;

        if (!par.match('}')) return false;

        m_entries.push_back(std::move(entry));
    }

    if (!par.match('}')) return false;

    return true;
}

namespace {
    bool Parser::parseEntry(BookmarkEntry& entry) {
        bool hasName = false;
        while (!match('}')) {
            std::string key = readString();
            if (!match(':')) return false;

            if (key == "name") {
                entry.name = readString();
                hasName = true;
            } else if (key == "xAxisColumn") {
                entry.xAxisColumn = static_cast<size_t>(readInt());
            } else if (key == "legendVisible") {
                entry.legendVisible = readBool();
            } else if (key == "dataItems") {
                if (!match('[')) return false;
                while (!match(']')) {
                    if (!entry.dataItems.empty() && !match(',')) return false;
                    std::string item = readString();
                    if (!item.empty())
                        entry.dataItems.push_back(item);
                }
            } else if (key == "graphs") {
                if (!match('[')) return false;
                while (!match(']')) {
                    if (!entry.graphs.empty() && !match(',')) return false;
                    if (!match('{')) return false;
                    GraphStyleSnapshot g;
                    if (!parseGraph(g)) return false;
                    if (!match('}')) return false;
                    entry.graphs.push_back(std::move(g));
                }
            } else if (key == "highlights") {
                if (!match('[')) return false;
                while (!match(']')) {
                    if (!entry.highlights.empty() && !match(',')) return false;
                    if (!match('{')) return false;
                    HighlightRule h;
                    if (!parseHighlight(h)) return false;
                    if (!match('}')) return false;
                    entry.highlights.push_back(std::move(h));
                }
            } else {
                // unknown key, skip value
                skipWS();
                if (p < end && *p == '"') { readString(); }
                else if (p < end && (*p == 't' || *p == 'f')) { readBool(); }
                else if (p < end && (*p == '-' || (*p >= '0' && *p <= '9'))) { readInt(); }
                else if (p < end && *p == '[') { match('['); while (!match(']')) { readString(); match(','); } }
                else if (p < end && *p == '{') { match('{'); int d=1; while (d>0 && p<end) { if (*p=='{') ++d; else if (*p=='}') --d; ++p; } }
            }
            match(','); // optional comma between keys
        }
        return hasName && !entry.name.empty();
    }

    bool Parser::parseGraph(GraphStyleSnapshot& g) {
        while (!match('}')) {
            std::string key = readString();
            if (!match(':')) return false;

            if (key == "name") g.dataItemName = readString();
            else if (key == "penStyle") g.penStyle = static_cast<int>(readInt());
            else if (key == "penWidth") g.penWidth = static_cast<int>(readInt());
            else if (key == "penColor") g.penColor = readString();
            else if (key == "scatterShape") g.scatterShape = static_cast<int>(readInt());
            else if (key == "scatterSize") g.scatterSize = static_cast<int>(readInt());
            else if (key == "scatterColor") g.scatterColor = readString();
            else if (key == "expressionText") g.expressionText = readString();
            else if (key == "isEdited") g.isEdited = readBool();
            match(',');
        }
        return !g.dataItemName.empty();
    }

    bool Parser::parseHighlight(HighlightRule& h) {
        while (!match('}')) {
            std::string key = readString();
            if (!match(':')) return false;

            if (key == "dataColumn") h.dataColumn = readString();
            else if (key == "condition") h.condition = static_cast<HighlightCondition>(readInt());
            else if (key == "value1") h.value1 = static_cast<double>(readInt());
            else if (key == "value2") h.value2 = static_cast<double>(readInt());
            else if (key == "color") h.color = QColor(QString::fromStdString(readString()));
            else if (key == "alpha") h.alpha = static_cast<int>(readInt());
            else if (key == "label") h.label = readString();
            match(',');
        }
        return true;
    }
}

// ---- 增删查 ----

bool BookmarkMgr::add(const BookmarkEntry& entry)
{
    if (entry.name.empty())
        return false;
    if (exists(entry.name))
        return false;
    m_entries.push_back(entry);
    return true;
}

bool BookmarkMgr::remove(const std::string& name)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
        [&name](const BookmarkEntry& e) { return e.name == name; });
    if (it == m_entries.end())
        return false;
    m_entries.erase(it);
    return true;
}

bool BookmarkMgr::exists(const std::string& name) const
{
    return find(name) != nullptr;
}

const BookmarkEntry* BookmarkMgr::find(const std::string& name) const
{
    for (const auto& e : m_entries)
        if (e.name == name)
            return &e;
    return nullptr;
}

} // namespace viewer