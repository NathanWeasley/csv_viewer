#pragma once

#include "code_viewer/datamgr/data_struct.hpp"
#include "code_qcp/qcustomplot.h"
#include <cmath>

namespace viewer
{

// ============================================================
// QCPChunkedGraph: 零拷贝 QCustomPlot plottable
// 
// 直接从 Column<VFLOAT> 读取数据，不创建任何中间缓冲区
// 支持 QCustomPlot 的缩放、拖拽操作实时更新显示
// ============================================================
class QCPChunkedGraph : public QCPAbstractPlottable, public QCPPlottableInterface1D
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

    QCPChunkedGraph(QCPAxis* keyAxis, QCPAxis* valueAxis);
    virtual ~QCPChunkedGraph() override;

    // ---- 设置数据列 ----
    // 设置 X 轴数据列（key）和 Y 轴数据列（value）
    // 列指针生命周期由 DataManager 管理，QCPChunkedGraph 不持有所有权
    void setDataColumns(const AbstractColumn* keyCol, const AbstractColumn* valueCol);

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

    // 从 chunked 列构建 QCPGraphData 块（用于绘图函数）
    // 返回一个局部 vector，仅用于 getLines 等绘图辅助函数
    QVector<QCPGraphData> fetchDataRange(int begin, int end) const;

private:
    const AbstractColumn* m_keyCol = nullptr;    // X 轴数据列指针（不拥有）
    const AbstractColumn* m_valueCol = nullptr;  // Y 轴数据列指针（不拥有）
    const AbstractColumn* m_valueLowCol = nullptr;  // 错误棒下限（可选）
    const AbstractColumn* m_valueHighCol = nullptr; // 错误棒上限（可选）

    LineStyle mLineStyle = lsLine;
    QCPScatterStyle mScatterStyle;
    bool mAdaptiveSampling = false;
};

} // namespace viewer