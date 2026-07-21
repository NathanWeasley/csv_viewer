#pragma once

#include "code_viewer/base/base_def.h"
#include "code_viewer/datamgr/data_struct.hpp"
#include "code_qcp/qcustomplot.h"
#include <cmath>
#include <qpainterpath.h>
#include <qpolygon.h>

namespace viewer
{

// ============================================================
// QCPColumnGraph: 零拷贝 QCustomPlot plottable
// 
// 直接从 Column<VFLOAT> 读取数据，不创建任何中间缓冲区
// 支持 QCustomPlot 的缩放、拖拽操作实时更新显示
// ============================================================
class VIEWER_API QCPColumnGraph
    : public QCPAbstractPlottable
    , public QCPPlottableInterface1D
{
    Q_OBJECT

public:
    // 线型（与 QCPGraph::LineStyle 一致）
    enum LineStyle
    {
        lsNone,        // 只绘制散点
        lsLine,        // 直线连接
        lsStepLeft,    // 左阶梯
        lsStepRight,   // 右阶梯
        lsStepCenter,  // 居中阶梯
        lsImpulse      // 脉冲线
    };

    QCPColumnGraph(QCPAxis* keyAxis, QCPAxis* valueAxis);
    virtual ~QCPColumnGraph() override;

    // ---- 自适应降采样全局开关 ----
    static bool s_adaptiveSamplingEnabled;

    // ---- 曲线与散点抗锯齿全局开关 ----
    static bool s_antiAliasingEnabled;

    // ---- 设置数据列 ----
    // 设置 X 轴数据列（key）和 Y 轴数据列（value）
    // 列指针生命周期由 DataManager 管理，QCPColumnGraph 不持有所有权
    void setDataColumns(const Column* keyCol, const Column* valueCol);

    // 通知 graph 底层数据已变更（例如表达式重新计算后），使范围缓存失效
    // 调用方应在调用后手动 replot() 以刷新显示
    void notifyDataChanged() { invalidateRangeCache(); }

    // ---- 线型设置 ----
    LineStyle lineStyle() const { return mLineStyle; }
    void setLineStyle(LineStyle style);

    QCPScatterStyle scatterStyle() const { return mScatterStyle; }
    void setScatterStyle(const QCPScatterStyle& style);

    // ---- QCPAbstractPlottable 接口 ----
    virtual double selectTest(const QPointF& pos, bool onlySelectable, QVariant* details = nullptr) const override;
    virtual QCPRange getKeyRange(bool& foundRange, QCP::SignDomain inSignDomain = QCP::sdBoth) const override;
    virtual QCPRange getValueRange(bool& foundRange, QCP::SignDomain inSignDomain = QCP::sdBoth,
                                   const QCPRange& inKeyRange = QCPRange()) const override;

    // ---- QCPPlottableInterface1D 接口 ----
    virtual int dataCount() const override;
    virtual double dataMainKey(int index) const override;
    virtual double dataSortKey(int index) const override;
    virtual double dataMainValue(int index) const override;
    virtual QCPRange dataValueRange(int index) const override;
    virtual QPointF dataPixelPosition(int index) const override;
    virtual bool sortKeyIsMainKey() const override;
    virtual QCPDataSelection selectTestRect(const QRectF& rect, bool onlySelectable) const override;
    virtual int findBegin(double sortKey, bool expandedRange = true) const override;
    virtual int findEnd(double sortKey, bool expandedRange = true) const override;

protected:

    // ---- QCPAbstractPlottable 内部接口 ----
    virtual void draw(QCPPainter* painter) override;
    virtual void drawLegendIcon(QCPPainter* painter, const QRectF& rect) const override;
    virtual QCP::Interaction selectionCategory() const override
    {
        // 没有不明确的数据点选中
        return QCP::iRangeDrag;
    }

    // ---- 绘图辅助 ----
    void getLines(QVector<QPointF>* lines, const QCPDataRange& dataRange) const;
    void getScatters(QVector<QPointF>* scatters, const QCPDataRange& dataRange) const;
    void drawLinePlot(QCPPainter* painter, const QVector<QPointF>& lines) const;
    void drawScatterPlot(QCPPainter* painter, const QVector<QPointF>& scatters) const;

    QVector<QPointF> dataToLines(const QVector<QCPGraphData>& data) const;
    QVector<QPointF> dataToStepLeftLines(const QVector<QCPGraphData>& data) const;
    QVector<QPointF> dataToStepRightLines(const QVector<QCPGraphData>& data) const;
    QVector<QPointF> dataToStepCenterLines(const QVector<QCPGraphData>& data) const;
    QVector<QPointF> dataToImpulseLines(const QVector<QCPGraphData>& data) const;

    // 可见数据范围（由当前坐标轴范围决定）
    QPair<int, int> getVisibleDataRange() const;

    // 从 Column 构建 QCPGraphData 块（用于绘图函数）
    QVector<QCPGraphData> fetchDataRange(int begin, int end) const;

    // 直接从 Column 读取数据并转换为像素坐标（跳过 QCPGraphData 中间层）
    void getLinesDirect(QVector<QPointF>* lines, int begin, int end) const;
    void getLinesDirectStyled(QVector<QPointF>* lines, int begin, int end) const;

    // 元素级降采样（O(N)）
    void getLinesDownsampled(QVector<QPointF>* lines, int begin, int end, int numBuckets) const;

    // 获取屏幕像素宽度对应的可见范围（用于降采样判断）
    int screenPixelWidth() const;

    // 重新计算 key/value 全量范围并更新缓存
    void recalculateRanges();
    void invalidateRangeCache();

private:
    const Column* m_keyCol = nullptr;
    const Column* m_valueCol = nullptr;
    const Column* m_valueLowCol = nullptr;
    const Column* m_valueHighCol = nullptr;

    LineStyle mLineStyle = lsLine;
    QCPScatterStyle mScatterStyle;
    bool mAdaptiveSampling = false;

    // 范围缓存
    mutable bool mRangeCacheValid = false;
    mutable double mCachedKeyMin = 0, mCachedKeyMax = 0;
    mutable double mCachedValueMin = 0, mCachedValueMax = 0;

    // 重绘热路径复用缓冲区，减少拖拽/缩放时的瞬时内存分配。
    mutable QVector<QPointF> m_lineBuffer;
    mutable QVector<QPointF> m_scatterBuffer;
    mutable QPolygonF m_fillPolygonBuffer;
    mutable QVector<QLineF> m_segmentBuffer;
    mutable QVector<QRectF> m_scatterRectBuffer;
    mutable QPainterPath m_scatterPathBuffer;

};

} // namespace viewer
