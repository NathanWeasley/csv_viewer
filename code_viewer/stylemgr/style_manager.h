#pragma once

#include "code_viewer/base/base_def.h"
#include "code_viewer/stylemgr/style_struct.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace viewer
{

// ============================================================
// StyleManager: 全局样式管理器
//
// 职责：
//   - 色板集中管理与动态切换
//   - 游标（临时/永久）风格定义
//   - 数据框风格定义
//   - JSON 持久化（exe路径/user/style.json）
//   - 首次运行自动初始化默认值
//
// 与 UI 层交互：
//   - onPaletteChanged 回调通知 UI 重建色板下拉
//   - UI 层通过 getter 读取当前样式值并应用到 Qt 控件
// ============================================================
class VIEWER_API StyleManager
{
public:
    StyleManager() = default;
    ~StyleManager() = default;

    // ---- 禁止拷贝 ----
    StyleManager(const StyleManager&) = delete;
    StyleManager& operator=(const StyleManager&) = delete;

    // ============================================================
    // 持久化
    // ============================================================

    /// 从 JSON 文件加载样式配置，失败时调用 initializeDefaults()
    bool load(const std::string& jsonPath);

    /// 保存当前样式配置到 JSON 文件
    bool save(const std::string& jsonPath) const;

    /// 初始化全部默认项（首次运行或无配置文件时调用）
    /// isDarkMode: 用于设置适合暗色/亮色主题的默认游标颜色
    void initializeDefaults(bool isDarkMode);

    /// 将游标和数据浮窗的配色适配到当前系统主题，同时保留尺寸、字体等设置。
    void applySystemTheme(bool isDarkMode);

    // ============================================================
    // 色板管理
    // ============================================================

    /// 所有可用色板
    const std::vector<ColorPalette>& palettes() const noexcept { return m_palettes; }

    /// 当前激活色板的 ID
    const std::string& activePaletteId() const noexcept { return m_activePaletteId; }

    /// 当前激活色板
    const ColorPalette& activePalette() const;

    /// 获取激活色板中的颜色，index 自动取模
    QColor paletteColorAt(size_t index) const;

    /// 获取激活色板的颜色数量
    size_t paletteColorCount() const;

    /// 切换激活色板，触发 onPaletteChanged
    void setActivePalette(const std::string& paletteId);

    // ============================================================
    // 游标风格管理
    // ============================================================

    /// 获取指定游标类型的风格定义
    const CursorStyleDef& cursorStyle(const std::string& id) const;

    /// 设置指定游标类型的风格定义
    void setCursorStyle(const std::string& id, const CursorStyleDef& style);

    // ============================================================
    // 数据框风格管理
    // ============================================================

    const DataBoxStyleDef& dataBoxStyle() const noexcept { return m_dataBoxStyle; }
    void setDataBoxStyle(const DataBoxStyleDef& style) { m_dataBoxStyle = style; }

    // ============================================================
    // Plot 主题管理
    // ============================================================

    /// 获取暗色/亮色主题
    const PlotTheme& plotTheme(bool isDark) const
    {
        return isDark ? m_themeDark : m_themeLight;
    }
    void setPlotTheme(bool isDark, const PlotTheme& theme)
    {
        if (isDark) m_themeDark = theme; else m_themeLight = theme;
    }

    // ============================================================
    // 回调
    // ============================================================

    /// 色板切换后触发，UI 层需重建色板下拉控件
    std::function<void()> onPaletteChanged;

private:
    using json = struct nlohmann_json_t; // forward declaration trick
    // We'll implement JSON I/O in .cpp with inline nlohmann or manual parsing

    std::vector<ColorPalette> m_palettes;
    std::string m_activePaletteId;

    std::map<std::string, CursorStyleDef> m_cursorStyles;
    DataBoxStyleDef m_dataBoxStyle;

    PlotTheme m_themeDark;
    PlotTheme m_themeLight;
};

} // namespace viewer
