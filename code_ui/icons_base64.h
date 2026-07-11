#pragma once

#include <cstdint>
#include <qstring.h>

#define ENUM2IDX(item)		(static_cast<int>(item))

// ============================================================
// 图标资源源类型
// ============================================================
enum class IconSource : uint8_t
{
	Base64,   // 内嵌 base64 数据 URI
	SvgFile,  // 外部 .svg 文件（通过 Qt 资源系统或文件路径加载）
};

// ============================================================
// 图标索引枚举（与工具栏按钮一一对应）
// ============================================================
enum class IconIdx : uint8_t
{
	LOADCSV = 0,
	LOADFOLDER,
	LOADDJIBIN,
	LOADHIKLOG,
	CLEAR,
	ADDEXPR,
	VARRENAME,
	NEWPLOT,
	GRIDVIEW,
	ROWVIEW,
	LINKX
};

// ============================================================
// IconEntry: 图标资源的统一描述
//   - 单一真相源：添加新图标只需在此表中加一行
//   - 支持两种加载方式：Base64 内嵌 或 SVG 文件
// ============================================================
struct IconEntry
{
	IconIdx      id;
	uint8_t      group;         // 分组编号：同组图标连续排列，换组时自动插入分隔符
	const char*  tooltip;       // 按钮提示文字
	IconSource   source;        // Base64 或 SvgFile
	const char*  data;          // Base64 data URI 或 Qt 资源路径（如 ":/data/icons/xxx.svg"）
};

// ============================================================
// 图标常量表
//   - 中性色 #323544 会在加载时自动被主题色替换（stroke + fill 均处理）
//   - 其他颜色（如 FFT 图标的 #2563eb、EXPR 的 #000000）保持不变
// ============================================================
static const IconEntry g_iconTable[] =
{
	// Group 0: 文件操作
	{
		IconIdx::LOADCSV,
		0,
		"Load CSVs",
		IconSource::SvgFile,
		":/icons/SVG/loadcsv.svg"
	},
	{
		IconIdx::LOADFOLDER,
		0,
		"Load Folders",
		IconSource::SvgFile,
		":/icons/SVG/loadfolder.svg"
	},
	{
		IconIdx::LOADDJIBIN,
		0,
		"Load Marked Binary",
		IconSource::SvgFile,
		":/icons/SVG/loadcfg.svg"
	},
	{
		IconIdx::LOADHIKLOG,
		0,
		"Load HikRobot Logs",
		IconSource::SvgFile,
		":/icons/SVG/loadhiklog.svg"
	},
	{
		IconIdx::CLEAR,
		0,
		"Clear All",
		IconSource::SvgFile,
		":/icons/SVG/clear.svg"
	},

	// Group 1: 
	{
		IconIdx::NEWPLOT,
		1,
		"新建图窗",
		IconSource::SvgFile,
		":/icons/SVG/newplot.svg"
	},
	{
		IconIdx::GRIDVIEW,
		1,
		"网格视图",
		IconSource::SvgFile,
		":/icons/SVG/gridview.svg"
	},
	{
		IconIdx::ROWVIEW,
		1,
		"纵向视图",
		IconSource::SvgFile,
		":/icons/SVG/rowview.svg"
	},
	{
		IconIdx::LINKX,
		1,
		"Tie X Axis Movements",
		IconSource::SvgFile,
		":/icons/SVG/linkx.svg"
	},

	// Group 2: 
	{
		IconIdx::ADDEXPR,
		2,
		"Add Global Expression",
		IconSource::SvgFile,
		":/icons/SVG/addexpr.svg"
	},

	// Group 3:
	{
		IconIdx::VARRENAME,
		3,
		"Rename Variable",
		IconSource::SvgFile,
		":/icons/SVG/varrename.svg"
	},
};

// 图标表项数
static constexpr int g_iconTableCount = sizeof(g_iconTable) / sizeof(g_iconTable[0]);

// ============================================================
// 中性色常量：SVG 中以此为标记的颜色会在加载时按主题替换
// ============================================================
// ============================================================
// 三色标记系统：设计 SVG 图标时使用以下标记色，运行时按主题自动替换
//   kColorStroke (#000000): 描边反差色 — 深色主题→浅灰, 浅色主题→深色
//   kColorFill   (#FFFFFF): 填充相容色 — 深色主题→深底, 浅色主题→浅底
//   其余颜色（如 #2563eb 等彩色）: 跨主题保持不变
// ============================================================
static constexpr const char* kColorStroke = "#000000";
static constexpr const char* kColorFill   = "#FFFFFF";
