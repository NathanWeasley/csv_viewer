#pragma once

#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

#include "code_viewer/base/base_def.h"

namespace viewer
{

// ============================================================
// PlotPageInfo: 单个图窗的逻辑状态（纯数据，无 Qt 依赖）
// ============================================================
struct PlotPageInfo
{
    std::string title;
    std::unordered_set<std::string> dataItems;  // 已绘制的 Y 列名集合（去重用）
    bool legendVisible = false;                  // 图例是否可见
    bool rectZoomActive = false;                 // 是否处于框选缩放模式
};

// ============================================================
// PlotManager: 多图窗管理器（纯 C++，无 Qt 依赖）
//
// 职责：
//   - 图窗（页面）增删管理
//   - 跟踪当前激活图窗
//   - 每个页面记录已添加的数据项（去重）
//   - 通过 std::function 回调通知 UI 层同步 Qt 控件
//
// UI 层（UI.cpp）负责：
//   - QTabWidget 容器管理
//   - QCustomPlot 实例创建/销毁
//   - QCPChunkedGraph 创建与数据绑定
//   - 监听 PlotManager 回调以同步视图
// ============================================================
class VIEWER_API PlotManager
{
public:
    PlotManager() = default;
    ~PlotManager() = default;

    // ---- 禁止拷贝 ----
    PlotManager(const PlotManager&) = delete;
    PlotManager& operator=(const PlotManager&) = delete;

    // ============================================================
    // 页面管理
    // ============================================================

    // 添加新图窗，返回新页面的索引
    int addPage(const std::string& title = "");

    // 移除图窗，返回是否成功
    bool removePage(int index);

    // 图窗数量
    int pageCount() const noexcept { return static_cast<int>(m_pages.size()); }
    bool empty() const noexcept { return m_pages.empty(); }

    // ============================================================
    // 激活页面
    // ============================================================

    // 获取当前激活页面的索引，无激活页面返回 -1
    int activePageIndex() const noexcept { return m_activeIndex; }
    bool hasActivePage() const noexcept { return m_activeIndex >= 0; }

    // 设置激活页面，超出范围无操作
    void setActivePage(int index);

    // ============================================================
    // 数据项管理（按页面索引操作）
    // ============================================================

    // 向指定页面添加数据项（自动去重），返回是否实际新增
    bool addDataItem(int pageIndex, const std::string& yColName);

    // 从指定页面移除数据项
    bool removeDataItem(int pageIndex, const std::string& yColName);

    // 检查指定页面是否已有该数据项
    bool hasDataItem(int pageIndex, const std::string& yColName) const;

    // 向激活页面添加数据项，无激活页面时自动创建
    bool addDataToActivePage(const std::string& yColName);

    // 清空指定页面的所有数据项
    void clearDataItems(int pageIndex);

    // 清空所有页面
    void clearAll();

    // ============================================================
    // 查询
    // ============================================================

    const PlotPageInfo& pageInfo(int index) const;
    PlotPageInfo& pageInfo(int index);

    const std::vector<PlotPageInfo>& pages() const noexcept { return m_pages; }

    // ============================================================
    // 回调（UI 层绑定以同步 Qt 控件）
    // ============================================================

    // 页面添加后触发，参数为新页面索引
    std::function<void(int index)> onPageAdded;

    // 页面即将被移除时触发，参数为被移除的页面索引
    std::function<void(int index)> onPageAboutToRemove;

    // 页面移除后触发，参数为新的激活页索引（可能为 -1）
    std::function<void(int activeIdx, int remainingCount)> onPageRemoved;

    // 激活页面变更后触发，参数为新激活页索引
    std::function<void(int index)> onActivePageChanged;

    // 数据项添加后触发，参数为 (页面索引, 列名)
    std::function<void(int pageIndex, const std::string& yColName)> onDataItemAdded;

    // 数据项移除后触发，参数为 (页面索引, 列名)
    std::function<void(int pageIndex, const std::string& yColName)> onDataItemRemoved;

    // 所有页面清空后触发
    std::function<void()> onCleared;

    // ============================================================
    // 图例管理
    // ============================================================

    // 设置指定页面的图例可见性
    void setLegendVisible(int pageIndex, bool visible);

    // 获取指定页面的图例可见性
    bool isLegendVisible(int pageIndex) const;

    // 图例可见性变更后触发，参数为 (页面索引, 是否可见)
    std::function<void(int pageIndex, bool visible)> onLegendVisibilityChanged;

    // 曲线样式变更后触发（UI 层需 replot 刷新图例图标）
    std::function<void(int pageIndex)> onLegendNeedReplot;

    // ============================================================
    // 框选缩放管理
    // ============================================================

    // 进入/退出框选缩放模式
    void setRectZoomActive(int pageIndex, bool active);

    // 查询框选缩放状态
    bool isRectZoomActive(int pageIndex) const;

    // 框选缩放状态变更后触发，参数为 (页面索引, 是否激活)
    std::function<void(int pageIndex, bool active)> onRectZoomStateChanged;

    // 还原缩放请求后触发
    std::function<void(int pageIndex)> onRescaleRequested;

private:
    std::string generatePageTitle() const;

    std::vector<PlotPageInfo> m_pages;
    int m_activeIndex = -1;
};

} // namespace viewer