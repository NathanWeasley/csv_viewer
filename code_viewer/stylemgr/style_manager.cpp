#include "code_viewer/stylemgr/style_manager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace viewer
{

// ============================================================
// Helpers: Color <-> QJsonValue
// ============================================================
static QJsonObject colorToJson(const Color& c)
{
    QJsonObject o;
    o["r"] = static_cast<int>(c.r);
    o["g"] = static_cast<int>(c.g);
    o["b"] = static_cast<int>(c.b);
    o["a"] = static_cast<int>(c.a);
    return o;
}

static Color colorFromJson(const QJsonObject& o)
{
    return Color(
        static_cast<uint8_t>(o["r"].toInt(0)),
        static_cast<uint8_t>(o["g"].toInt(0)),
        static_cast<uint8_t>(o["b"].toInt(0)),
        static_cast<uint8_t>(o["a"].toInt(255)));
}

static const char* shapeToString(CursorStyleDef::Shape s)
{
    switch (s)
    {
        case CursorStyleDef::Square:  return "square";
        case CursorStyleDef::Diamond: return "diamond";
        default:                       return "circle";
    }
}

static CursorStyleDef::Shape shapeFromString(const std::string& s)
{
    if (s == "square")  return CursorStyleDef::Square;
    if (s == "diamond") return CursorStyleDef::Diamond;
    return CursorStyleDef::Circle;
}

// ============================================================
// 持久化
// ============================================================

bool StyleManager::load(const std::string& jsonPath)
{
    QFile file(QString::fromStdString(jsonPath));
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError)
        return false;

    QJsonObject root = doc.object();

    // ---- activePalette ----
    m_activePaletteId = root["activePalette"].toString("matlab35").toStdString();

    // ---- palettes ----
    m_palettes.clear();
    QJsonArray palArr = root["palettes"].toArray();
    for (const auto& val : palArr)
    {
        QJsonObject pobj = val.toObject();
        ColorPalette cp;
        cp.id   = pobj["id"].toString("unknown").toStdString();
        cp.name = pobj["name"].toString("Unknown").toStdString();

        QJsonArray colsArr = pobj["colors"].toArray();
        for (const auto& cval : colsArr)
        {
            cp.colors.push_back(colorFromJson(cval.toObject()));
        }
        // 跳过空色板
        if (!cp.colors.empty())
            m_palettes.push_back(std::move(cp));
    }

    // If no palettes loaded, initialize defaults
    if (m_palettes.empty())
        return false; // caller should call initializeDefaults()

    // Validate activePaletteId
    bool found = false;
    for (auto& cp : m_palettes)
    {
        if (cp.id == m_activePaletteId) { found = true; break; }
    }
    if (!found)
        m_activePaletteId = m_palettes.front().id;

    // ---- cursorStyles ----
    m_cursorStyles.clear();
    QJsonObject csObj = root["cursorStyles"].toObject();
    for (auto it = csObj.begin(); it != csObj.end(); ++it)
    {
        QJsonObject cobj = it.value().toObject();
        CursorStyleDef cs;
        cs.id    = it.key().toStdString();
        cs.shape = shapeFromString(cobj["shape"].toString("circle").toStdString());
        cs.size  = static_cast<float>(cobj["size"].toDouble(7.0));

        cs.fillActive   = colorFromJson(cobj["fillActive"].toObject());
        cs.fillInactive = colorFromJson(cobj["fillInactive"].toObject());

        cs.strokeActive   = colorFromJson(cobj["strokeActive"].toObject());
        cs.strokeInactive = colorFromJson(cobj["strokeInactive"].toObject());

        cs.strokeWidthActive   = static_cast<float>(cobj["strokeWidthActive"].toDouble(1.5));
        cs.strokeWidthInactive = static_cast<float>(cobj["strokeWidthInactive"].toDouble(0.0));

        cs.opacityActive   = static_cast<float>(cobj["opacityActive"].toDouble(1.0));
        cs.opacityInactive = static_cast<float>(cobj["opacityInactive"].toDouble(0.31));

        m_cursorStyles[cs.id] = std::move(cs);
    }

    // ---- dataBoxStyle ----
    QJsonObject dbObj = root["dataBoxStyle"].toObject();
    if (!dbObj.isEmpty())
    {
        DataBoxStyleDef db;
        db.fontFamily  = dbObj["fontFamily"].toString("Consolas").toStdString();
        db.fontSize    = dbObj["fontSize"].toInt(9);
        db.textColor   = colorFromJson(dbObj["textColor"].toObject());
        db.bgColor     = colorFromJson(dbObj["bgColor"].toObject());
        db.bgAlpha     = dbObj["bgAlpha"].toInt(220);
        db.borderActive   = colorFromJson(dbObj["borderActive"].toObject());
        db.borderInactive = colorFromJson(dbObj["borderInactive"].toObject());
        db.borderWidthActive   = static_cast<float>(dbObj["borderWidthActive"].toDouble(2.0));
        db.borderWidthInactive = static_cast<float>(dbObj["borderWidthInactive"].toDouble(1.0));
        db.padLeft   = dbObj["padLeft"].toInt(6);
        db.padRight  = dbObj["padRight"].toInt(6);
        db.padTop    = dbObj["padTop"].toInt(3);
        db.padBottom = dbObj["padBottom"].toInt(3);
        m_dataBoxStyle = db;
    }

    // ---- plotThemeDark ----
    {
        QJsonObject ptd = root["plotThemeDark"].toObject();
        if (!ptd.isEmpty())
        {
            m_themeDark.bgColor        = colorFromJson(ptd["bgColor"].toObject());
            m_themeDark.axisLabelColor = colorFromJson(ptd["axisLabelColor"].toObject());
            m_themeDark.tickLabelColor = colorFromJson(ptd["tickLabelColor"].toObject());
            m_themeDark.basePenColor   = colorFromJson(ptd["basePenColor"].toObject());
            m_themeDark.basePenWidth   = static_cast<float>(ptd["basePenWidth"].toDouble(1.0));
        }
        else
        {
            m_themeDark.bgColor        = Color(0x2d, 0x2d, 0x2d);
            m_themeDark.axisLabelColor = Color(0xcc, 0xcc, 0xcc);
            m_themeDark.tickLabelColor = Color(0xaa, 0xaa, 0xaa);
            m_themeDark.basePenColor   = Color(0x3a, 0x3a, 0x3a);
            m_themeDark.basePenWidth   = 1.0f;
        }
    }

    // ---- plotThemeLight ----
    {
        QJsonObject ptl = root["plotThemeLight"].toObject();
        if (!ptl.isEmpty())
        {
            m_themeLight.bgColor        = colorFromJson(ptl["bgColor"].toObject());
            m_themeLight.axisLabelColor = colorFromJson(ptl["axisLabelColor"].toObject());
            m_themeLight.tickLabelColor = colorFromJson(ptl["tickLabelColor"].toObject());
            m_themeLight.basePenColor   = colorFromJson(ptl["basePenColor"].toObject());
            m_themeLight.basePenWidth   = static_cast<float>(ptl["basePenWidth"].toDouble(1.0));
        }
        else
        {
            m_themeLight.bgColor        = Color(0xff, 0xff, 0xff);
            m_themeLight.axisLabelColor = Color(0x33, 0x33, 0x33);
            m_themeLight.tickLabelColor = Color(0x55, 0x55, 0x55);
            m_themeLight.basePenColor   = Color(0xaa, 0xaa, 0xaa);
            m_themeLight.basePenWidth   = 1.0f;
        }
    }

    return true;
}

bool StyleManager::save(const std::string& jsonPath) const
{
    QJsonObject root;

    // activePalette
    root["activePalette"] = QString::fromStdString(m_activePaletteId);

    // palettes
    QJsonArray palArr;
    for (const auto& cp : m_palettes)
    {
        QJsonObject pobj;
        pobj["id"]   = QString::fromStdString(cp.id);
        pobj["name"] = QString::fromStdString(cp.name);

        QJsonArray colsArr;
        for (const auto& c : cp.colors)
            colsArr.append(colorToJson(c));
        pobj["colors"] = colsArr;

        palArr.append(pobj);
    }
    root["palettes"] = palArr;

    // cursorStyles
    QJsonObject csObj;
    for (const auto& pair : m_cursorStyles)
    {
        const auto& cs = pair.second;
        QJsonObject cobj;
        cobj["shape"] = QString::fromLatin1(shapeToString(cs.shape));
        cobj["size"]  = static_cast<double>(cs.size);

        cobj["fillActive"]   = colorToJson(cs.fillActive);
        cobj["fillInactive"] = colorToJson(cs.fillInactive);

        cobj["strokeActive"]   = colorToJson(cs.strokeActive);
        cobj["strokeInactive"] = colorToJson(cs.strokeInactive);

        cobj["strokeWidthActive"]   = static_cast<double>(cs.strokeWidthActive);
        cobj["strokeWidthInactive"] = static_cast<double>(cs.strokeWidthInactive);

        cobj["opacityActive"]   = static_cast<double>(cs.opacityActive);
        cobj["opacityInactive"] = static_cast<double>(cs.opacityInactive);

        csObj[QString::fromStdString(cs.id)] = cobj;
    }
    root["cursorStyles"] = csObj;

    // dataBoxStyle
    {
        QJsonObject db;
        db["fontFamily"] = QString::fromStdString(m_dataBoxStyle.fontFamily);
        db["fontSize"]   = m_dataBoxStyle.fontSize;
        db["textColor"]  = colorToJson(m_dataBoxStyle.textColor);
        db["bgColor"]    = colorToJson(m_dataBoxStyle.bgColor);
        db["bgAlpha"]    = m_dataBoxStyle.bgAlpha;
        db["borderActive"]   = colorToJson(m_dataBoxStyle.borderActive);
        db["borderInactive"] = colorToJson(m_dataBoxStyle.borderInactive);
        db["borderWidthActive"]   = static_cast<double>(m_dataBoxStyle.borderWidthActive);
        db["borderWidthInactive"] = static_cast<double>(m_dataBoxStyle.borderWidthInactive);
        db["padLeft"]   = m_dataBoxStyle.padLeft;
        db["padRight"]  = m_dataBoxStyle.padRight;
        db["padTop"]    = m_dataBoxStyle.padTop;
        db["padBottom"] = m_dataBoxStyle.padBottom;
        root["dataBoxStyle"] = db;
    }

    // plotThemeDark
    {
        QJsonObject pt;
        pt["bgColor"]        = colorToJson(m_themeDark.bgColor);
        pt["axisLabelColor"] = colorToJson(m_themeDark.axisLabelColor);
        pt["tickLabelColor"] = colorToJson(m_themeDark.tickLabelColor);
        pt["basePenColor"]   = colorToJson(m_themeDark.basePenColor);
        pt["basePenWidth"]   = static_cast<double>(m_themeDark.basePenWidth);
        root["plotThemeDark"] = pt;
    }

    // plotThemeLight
    {
        QJsonObject pt;
        pt["bgColor"]        = colorToJson(m_themeLight.bgColor);
        pt["axisLabelColor"] = colorToJson(m_themeLight.axisLabelColor);
        pt["tickLabelColor"] = colorToJson(m_themeLight.tickLabelColor);
        pt["basePenColor"]   = colorToJson(m_themeLight.basePenColor);
        pt["basePenWidth"]   = static_cast<double>(m_themeLight.basePenWidth);
        root["plotThemeLight"] = pt;
    }

    QJsonDocument doc(root);

    // Ensure directory exists
    QDir dir = QFileInfo(QString::fromStdString(jsonPath)).absoluteDir();
    if (!dir.exists())
        dir.mkpath(".");

    QFile file(QString::fromStdString(jsonPath));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    return true;
}

// ============================================================
// 默认初始化
// ============================================================

void StyleManager::initializeDefaults(bool isDarkMode)
{
    // ========================================
    // 色板 1: MATLAB 35 Colors
    // ========================================
    {
        ColorPalette cp;
        cp.id   = "matlab35";
        cp.name = "MATLAB 35 Colors";
        cp.colors = {
            // Row 1: MATLAB 7 色
            Color(0, 114, 189),     // 0:  MATLAB Blue
            Color(217, 83, 25),     // 1:  MATLAB Orange
            Color(237, 177, 32),    // 2:  MATLAB Yellow
            Color(126, 47, 142),    // 3:  MATLAB Purple
            Color(119, 172, 48),    // 4:  MATLAB Green
            Color(77, 190, 238),    // 5:  MATLAB Cyan
            Color(162, 20, 47),     // 6:  MATLAB Red
            // Row 2: Deep saturated
            Color(0, 147, 147),     // 7:  Teal
            Color(255, 61, 127),    // 8:  Pink
            Color(100, 149, 237),   // 9:  Cornflower
            Color(205, 92, 92),     // 10: Indian Red
            Color(85, 107, 47),     // 11: Olive Drab
            Color(186, 85, 211),    // 12: Medium Orchid
            // Row 3: Bright accents
            Color(0, 191, 255),     // 13: Deep Sky Blue
            Color(255, 215, 0),     // 14: Gold
            Color(50, 205, 50),     // 15: Lime Green
            Color(255, 99, 71),     // 16: Tomato
            Color(64, 224, 208),    // 17: Turquoise
            Color(255, 140, 0),     // 18: Dark Orange
            // Row 4: Rich tones
            Color(138, 43, 226),    // 19: Blue Violet
            Color(0, 206, 209),     // 20: Dark Turquoise
            Color(255, 20, 147),    // 21: Deep Pink
            Color(154, 205, 50),    // 22: Yellow Green
            Color(70, 130, 180),    // 23: Steel Blue
            Color(240, 128, 128),   // 24: Light Coral
            // Row 5: More
            Color(147, 112, 219),   // 25: Medium Purple
            Color(60, 179, 113),    // 26: Medium Sea Green
            Color(255, 160, 122),   // 27: Light Salmon
            Color(0, 191, 143),     // 28: Mint
            Color(255, 69, 0),      // 29: Orange Red
            Color(65, 105, 225),    // 30: Royal Blue
            Color(218, 165, 32),    // 31: Goldenrod
            // End markers
            Color(0, 0, 0),         // 32: Black
            Color(128, 128, 128),   // 33: Gray
            Color(192, 192, 192)    // 34: Silver
        };
        m_palettes.push_back(std::move(cp));
    }

    // ========================================
    // 色板 2: Tableau 10
    // ========================================
    {
        ColorPalette cp;
        cp.id   = "tableau10";
        cp.name = "Tableau 10";
        cp.colors = {
            Color(31, 119, 180),
            Color(255, 127, 14),
            Color(44, 160, 44),
            Color(214, 39, 40),
            Color(148, 103, 189),
            Color(140, 86, 75),
            Color(227, 119, 194),
            Color(127, 127, 127),
            Color(188, 189, 34),
            Color(23, 190, 207)
        };
        m_palettes.push_back(std::move(cp));
    }

    // ========================================
    // 色板 3: Pastel
    // ========================================
    {
        ColorPalette cp;
        cp.id   = "pastel";
        cp.name = "Pastel";
        cp.colors = {
            Color(166, 206, 227),
            Color(31, 120, 180),
            Color(178, 223, 138),
            Color(51, 160, 44),
            Color(251, 154, 153),
            Color(227, 26, 28),
            Color(253, 191, 111),
            Color(255, 127, 0),
            Color(202, 178, 214),
            Color(106, 61, 154),
            Color(255, 255, 153),
            Color(177, 89, 40)
        };
        m_palettes.push_back(std::move(cp));
    }

    // ========================================
    // 色板 4: Colorblind Safe (8 colors)
    // ========================================
    {
        ColorPalette cp;
        cp.id   = "colorblind";
        cp.name = "Colorblind Safe";
        cp.colors = {
            Color(0, 73, 73),       // #004949
            Color(0, 109, 219),     // #006ddb
            Color(146, 0, 0),       // #920000
            Color(73, 0, 146),      // #490092
            Color(219, 209, 0),     // #dbd100
            Color(0, 146, 146),     // #009292
            Color(255, 109, 182),   // #ff6db6
            Color(182, 109, 255)    // #b66dff
        };
        m_palettes.push_back(std::move(cp));
    }

    // Set default active palette
    m_activePaletteId = "matlab35";

    // ========================================
    // 游标风格
    // ========================================

    // --- temporary (预选 hover) ---
    {
        CursorStyleDef cs;
        cs.id    = "temporary";
        cs.shape = CursorStyleDef::Circle;
        cs.size  = 8.0f;

        if (isDarkMode)
        {
            cs.fillActive   = Color(0xFF, 0xD7, 0x00, 180);
            cs.fillInactive = Color(0xFF, 0xD7, 0x00, 80);
            cs.strokeActive   = Color(0x44, 0x44, 0x44);
            cs.strokeInactive = Color(0, 0, 0, 0);
        }
        else
        {
            cs.fillActive   = Color(0x22, 0x66, 0xcc, 120);
            cs.fillInactive = Color(0x22, 0x66, 0xcc, 60);
            cs.strokeActive   = Color(0x22, 0x66, 0xcc);
            cs.strokeInactive = Color(0, 0, 0, 0);
        }
        cs.strokeWidthActive   = 2.5f;
        cs.strokeWidthInactive = 0.0f;
        cs.opacityActive   = 1.0f;
        cs.opacityInactive = 0.31f;

        m_cursorStyles["temporary"] = std::move(cs);
    }

    // --- permanent (游标标记) ---
    {
        CursorStyleDef cs;
        cs.id    = "permanent";
        cs.shape = CursorStyleDef::Circle;
        cs.size  = 7.0f;

        if (isDarkMode)
        {
            cs.fillActive   = Color(0xFF, 0xD7, 0x00);
            cs.fillInactive = Color(0xFF, 0xD7, 0x00, 80);
            cs.strokeActive   = Color(0xFF, 0xD7, 0x00);
            cs.strokeInactive = Color(0, 0, 0, 0);
        }
        else
        {
            cs.fillActive   = Color(0x22, 0x66, 0xcc);
            cs.fillInactive = Color(0x22, 0x66, 0xcc, 80);
            cs.strokeActive   = Color(0x22, 0x66, 0xcc);
            cs.strokeInactive = Color(0, 0, 0, 0);
        }
        cs.strokeWidthActive   = 1.5f;
        cs.strokeWidthInactive = 0.0f;
        cs.opacityActive   = 1.0f;
        cs.opacityInactive = 0.31f;

        m_cursorStyles["permanent"] = std::move(cs);
    }

    // ========================================
    // 数据框风格
    // ========================================
    {
        DataBoxStyleDef db;
        db.fontFamily  = "Consolas";
        db.fontSize    = 9;

        if (isDarkMode)
        {
            db.textColor    = Color(0xFF, 0xD7, 0x00);
            db.bgColor      = Color(0x2d, 0x2d, 0x2d, 220);
            db.bgAlpha      = 220;
            db.borderActive   = Color(0xFF, 0xD7, 0x00);
            db.borderInactive = Color(0x88, 0x88, 0x88);
        }
        else
        {
            db.textColor    = Color(0x22, 0x66, 0xcc);
            db.bgColor      = Color(245, 245, 245, 230);
            db.bgAlpha      = 230;
            db.borderActive   = Color(0x22, 0x66, 0xcc);
            db.borderInactive = Color(0x88, 0x88, 0x88);
        }
        db.borderWidthActive   = 2.0f;
        db.borderWidthInactive = 1.0f;
        db.padLeft   = 6;
        db.padRight  = 6;
        db.padTop    = 3;
        db.padBottom = 3;

        m_dataBoxStyle = db;
    }

    // ========================================
    // Plot 主题
    // ========================================
    {
        // Dark theme
        m_themeDark.bgColor        = Color(0x2d, 0x2d, 0x2d);
        m_themeDark.axisLabelColor = Color(0xcc, 0xcc, 0xcc);
        m_themeDark.tickLabelColor = Color(0xaa, 0xaa, 0xaa);
        m_themeDark.basePenColor   = Color(0x3a, 0x3a, 0x3a);
        m_themeDark.basePenWidth   = 1.0f;

        // Light theme
        m_themeLight.bgColor        = Color(0xff, 0xff, 0xff);
        m_themeLight.axisLabelColor = Color(0x33, 0x33, 0x33);
        m_themeLight.tickLabelColor = Color(0x55, 0x55, 0x55);
        m_themeLight.basePenColor   = Color(0xaa, 0xaa, 0xaa);
        m_themeLight.basePenWidth   = 1.0f;
    }
}

// ============================================================
// 色板管理
// ============================================================

const ColorPalette& StyleManager::activePalette() const
{
    for (const auto& cp : m_palettes)
    {
        if (cp.id == m_activePaletteId)
            return cp;
    }
    // fallback: return first palette
    if (!m_palettes.empty())
        return m_palettes.front();

    static ColorPalette empty;
    return empty;
}

QColor StyleManager::paletteColorAt(size_t index) const
{
    return activePalette().colorAt(index).toQColor();
}

size_t StyleManager::paletteColorCount() const
{
    return activePalette().colors.size();
}

void StyleManager::setActivePalette(const std::string& paletteId)
{
    if (m_activePaletteId == paletteId)
        return;

    // Validate
    bool found = false;
    for (const auto& cp : m_palettes)
    {
        if (cp.id == paletteId) { found = true; break; }
    }
    if (!found)
        return;

    m_activePaletteId = paletteId;

    if (onPaletteChanged)
        onPaletteChanged();
}

// ============================================================
// 游标风格管理
// ============================================================

const CursorStyleDef& StyleManager::cursorStyle(const std::string& id) const
{
    auto it = m_cursorStyles.find(id);
    if (it != m_cursorStyles.end())
        return it->second;

    // fallback: return default temporary style
    static CursorStyleDef fallback;
    return fallback;
}

void StyleManager::setCursorStyle(const std::string& id, const CursorStyleDef& style)
{
    m_cursorStyles[id] = style;
}

} // namespace viewer