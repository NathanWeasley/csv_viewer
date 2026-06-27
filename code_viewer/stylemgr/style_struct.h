#pragma once

#include "code_viewer/base/base_def.h"

#include <QColor>
#include <QFont>
#include <string>
#include <vector>
#include <cstdint>

namespace viewer
{

// ============================================================
// Color: RGB 颜色值（无 QColor 依赖，便于 JSON 序列化）
// ============================================================
struct VIEWER_API Color
{
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;

    Color() = default;
    Color(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}

    explicit Color(const QColor& qc)
        : r(static_cast<uint8_t>(qc.red()))
        , g(static_cast<uint8_t>(qc.green()))
        , b(static_cast<uint8_t>(qc.blue()))
        , a(static_cast<uint8_t>(qc.alpha())) {}

    QColor toQColor() const { return QColor(r, g, b, a); }

    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const Color& o) const { return !(*this == o); }
};

// ============================================================
// ColorPalette: 一套色板
// ============================================================
struct VIEWER_API ColorPalette
{
    std::string name;               // 显示名称，如 "MATLAB 35"
    std::string id;                 // 唯一标识，如 "matlab35"
    std::vector<Color> colors;      // 色板颜色列表

    // 取色，自动取模
    Color colorAt(size_t index) const
    {
        if (colors.empty()) return Color();
        return colors[index % colors.size()];
    }
};

// ============================================================
// CursorStyleDef: 游标标记风格定义
// ============================================================
struct VIEWER_API CursorStyleDef
{
    std::string id;   // "temporary" / "permanent"

    // 标记形状
    enum Shape { Circle = 0, Square, Diamond };
    Shape shape = Circle;
    float size = 7.0f;                 // 默认大小（px）

    // 填充
    Color fillActive;
    Color fillInactive;

    // 描边
    Color strokeActive;
    Color strokeInactive;
    float strokeWidthActive = 1.5f;
    float strokeWidthInactive = 0.0f;

    // 不透明度辅助（用于 brush 的 alpha 叠加）
    float opacityActive   = 1.0f;
    float opacityInactive = 0.31f;
};

// ============================================================
// PlotTheme: QCustomPlot 主题配色
// ============================================================
struct VIEWER_API PlotTheme
{
    Color bgColor;          // 背景色
    Color axisLabelColor;   // 轴标签颜色
    Color tickLabelColor;   // 刻度标签颜色
    Color basePenColor;     // 轴/刻度/子刻度笔颜色
    float basePenWidth = 1.0f; // 笔宽
};

// ============================================================
// DataBoxStyleDef: 数据框（QCPItemText 标签）风格定义
// ============================================================
struct VIEWER_API DataBoxStyleDef
{
    // 文字
    std::string fontFamily = "Consolas";
    int fontSize = 9;
    Color textColor;           // 文字颜色

    // 背景
    Color bgColor;             // 背景填充色
    int bgAlpha = 220;

    // 边框
    Color borderActive;
    Color borderInactive;
    float borderWidthActive   = 2.0f;
    float borderWidthInactive = 1.0f;

    // 内边距
    int padLeft   = 6;
    int padRight  = 6;
    int padTop    = 3;
    int padBottom = 3;

    QFont toQFont() const
    {
        QFont f(QString::fromStdString(fontFamily), fontSize);
        f.setStyleHint(QFont::Monospace);
        return f;
    }
};

} // namespace viewer