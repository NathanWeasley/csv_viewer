#pragma once

#include "code_viewer/base/base_def.h"

#include <functional>
#include <string>
#include <vector>

namespace viewer
{

// ============================================================
// CursorInfo: 单个游标的数据
// ============================================================
struct CursorInfo
{
    int         pageIndex     = -1;       // 所属图窗索引
    std::string dataItemName;             // Y列名（对应 QCPColumnGraph::name()）
    size_t      dataIndex     = 0;        // 数据行索引
    bool        isActive      = false;
};

// ============================================================
// CursorManager: 数据游标管理器（纯 C++，无 Qt 依赖）
//
// 职责：
//   - 预选（hover）状态管理
//   - 游标增删/激活管理
//   - 通过 std::function 回调通知 UI 层
//
// UI 层负责：
//   - QCPItemTracer 创建/显隐（预选标记）
//   - QWidget 浮窗创建/销毁/定位
//   - 鼠标/键盘事件处理并调用 CursorManager 接口
// ============================================================
class VIEWER_API CursorManager
{
public:
    CursorManager() = default;
    ~CursorManager() = default;

    // ---- 禁止拷贝 ----
    CursorManager(const CursorManager&) = delete;
    CursorManager& operator=(const CursorManager&) = delete;

    // ============================================================
    // 预选管理
    // ============================================================

    // 设置当前预选的数据点
    // 若与当前预选相同则忽略，不同则替换
    void setPreSelection(int pageIndex, const std::string& dataItemName, size_t dataIndex);

    // 清除预选状态（若当前无预选则忽略）
    void clearPreSelection();

    // 查询：是否有预选
    bool hasPreSelection() const noexcept { return m_hasPreSel; }

    // 查询预选信息
    int         preSelPage()  const noexcept { return m_preSelPage; }
    const std::string& preSelItem() const noexcept { return m_preSelItem; }
    size_t      preSelIndex() const noexcept { return m_preSelIndex; }

    // ============================================================
    // 游标管理
    // ============================================================

    // 添加游标并返回其索引。新游标自动设为激活
    int  addCursor(int pageIndex, const std::string& dataItemName, size_t dataIndex);

    // 按索引删除游标
    bool removeCursor(int index);

    // 删除当前激活的游标（无激活则无操作）
    bool removeActiveCursor();

    // 设置激活游标，index = -1 表示取消所有激活
    void setActiveCursor(int index);

    // 移动指定游标到新的数据索引（新游标触发 onActiveCursorChanged）
    bool moveCursorToIndex(int cursorIdx, size_t newDataIndex);

    // 清除所有游标
    void clearAll();

    void shiftPageIndicesAfterRemoval(int removedPageIndex);

    // ============================================================
    // 查询
    // ============================================================

    int  activeCursorIndex() const noexcept { return m_activeCursorIdx; }
    bool hasActiveCursor()   const noexcept { return m_activeCursorIdx >= 0; }

    const std::vector<CursorInfo>& cursors() const noexcept { return m_cursors; }
    size_t cursorCount() const noexcept { return m_cursors.size(); }

    // ============================================================
    // 回调（UI 层绑定以同步 Qt 控件）
    // ============================================================

    // 预选设置后触发
    // 参数: (页面索引, 数据项名, 数据索引)
    std::function<void(int pageIndex, const std::string& dataItemName, size_t dataIndex)> onPreSelectionSet;

    // 预选清除后触发
    std::function<void()> onPreSelectionCleared;

    // 游标添加后触发
    // 参数: (游标索引, 页面索引, 数据项名, 数据索引)
    std::function<void(int cursorIdx, int pageIndex, const std::string& dataItemName, size_t dataIndex)> onCursorAdded;

    // 游标移除后触发
    // 参数: (游标索引)
    std::function<void(int cursorIdx)> onCursorRemoved;

    // 激活游标变更后触发
    // 参数: (游标索引, -1表示无激活)
    std::function<void(int cursorIdx)> onActiveCursorChanged;

private:
    // 预选状态
    bool        m_hasPreSel   = false;
    int         m_preSelPage  = -1;
    std::string m_preSelItem;
    size_t      m_preSelIndex = 0;

    // 游标列表
    std::vector<CursorInfo> m_cursors;

    // 激活游标索引（-1 = 无激活）
    int m_activeCursorIdx = -1;
};

} // namespace viewer