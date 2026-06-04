#include "code_viewer/datamgr/qcp_chunked_graph.h"
#include <limits>

namespace viewer
{

// ============================================================
// 构造 / 析构
// ============================================================
QCPChunkedGraph::QCPChunkedGraph(QCPAxis* keyAxis, QCPAxis* valueAxis)
    : QCPAbstractPlottable(keyAxis, valueAxis)
{
    mName = "ChunkedGraph";
    // 默认散点样式：不显示散点
    mScatterStyle = QCPScatterStyle(QCPScatterStyle::ssNone);
}

QCPChunkedGraph::~QCPChunkedGraph()
{
    // 不拥有数据列指针，不需要释放
}

// ============================================================
// 设置数据列
// ============================================================
void QCPChunkedGraph::setDataColumns(const AbstractColumn* keyCol, const AbstractColumn* valueCol)
{
    m_keyCol = keyCol;
    m_valueCol = valueCol;
}

void QCPChunkedGraph::setLineStyle(LineStyle style)
{
    mLineStyle = style;
}

void QCPChunkedGraph::setScatterStyle(const QCPScatterStyle& style)
{
    mScatterStyle = style;
}

// ============================================================
// QCPPlottableInterface1D 接口
// ============================================================

int QCPChunkedGraph::dataCount() const
{
    if (!m_keyCol) return 0;
    return static_cast<int>(m_keyCol->size());
}

double QCPChunkedGraph::dataMainKey(int index) const
{
    if (!m_keyCol) return 0;
    return m_keyCol->getDouble(static_cast<size_t>(index));
}

double QCPChunkedGraph::dataSortKey(int index) const
{
    // 对于普通线图，sort key = main key
    return dataMainKey(index);
}

double QCPChunkedGraph::dataMainValue(int index) const
{
    if (!m_valueCol) return 0;
    return m_valueCol->getDouble(static_cast<size_t>(index));
}

QCPRange QCPChunkedGraph::dataValueRange(int index) const
{
    double v = dataMainValue(index);
    return QCPRange(v, v);
}

QPointF QCPChunkedGraph::dataPixelPosition(int index) const
{
    return coordsToPixels(dataMainKey(index), dataMainValue(index));
}

bool QCPChunkedGraph::sortKeyIsMainKey() const
{
    return true;
}

QCPDataSelection QCPChunkedGraph::selectTestRect(const QRectF& rect, bool onlySelectable) const
{
    QCPDataSelection result;
    if ((onlySelectable && mSelectable == QCP::stNone) || !m_keyCol || m_keyCol->empty())
        return result;
    if (!mKeyAxis || !mValueAxis)
        return result;

    double key1, value1, key2, value2;
    pixelsToCoords(rect.topLeft(), key1, value1);
    pixelsToCoords(rect.bottomRight(), key2, value2);
    QCPRange keyRange(key1, key2);
    QCPRange valueRange(value1, value2);

    const size_t n = m_keyCol->size();
    int currentSegmentBegin = -1;

    for (size_t i = 0; i < n; ++i)
    {
        double k = m_keyCol->getDouble(i);
        double v = m_valueCol->getDouble(i);

        if (currentSegmentBegin == -1)
        {
            if (valueRange.contains(v) && keyRange.contains(k))
                currentSegmentBegin = static_cast<int>(i);
        }
        else if (!valueRange.contains(v) || !keyRange.contains(k))
        {
            result.addDataRange(QCPDataRange(currentSegmentBegin, static_cast<int>(i)), false);
            currentSegmentBegin = -1;
        }
    }
    if (currentSegmentBegin != -1)
        result.addDataRange(QCPDataRange(currentSegmentBegin, static_cast<int>(n)), false);

    result.simplify();
    return result;
}

int QCPChunkedGraph::findBegin(double sortKey, bool expandedRange) const
{
    if (!m_keyCol || m_keyCol->empty())
        return 0;

    const size_t n = m_keyCol->size();

    // 二分查找
    size_t low = 0;
    size_t high = n;
    while (low < high)
    {
        size_t mid = low + (high - low) / 2;
        if (m_keyCol->getDouble(mid) < sortKey)
            low = mid + 1;
        else
            high = mid;
    }

    // 确保 key 列是排序的
    if (expandedRange && low > 0)
        --low;

    return static_cast<int>(low);
}

int QCPChunkedGraph::findEnd(double sortKey, bool expandedRange) const
{
    if (!m_keyCol || m_keyCol->empty())
        return 0;

    const size_t n = m_keyCol->size();

    size_t low = 0;
    size_t high = n;
    while (low < high)
    {
        size_t mid = low + (high - low) / 2;
        if (m_keyCol->getDouble(mid) <= sortKey)
            low = mid + 1;
        else
            high = mid;
    }

    if (expandedRange && low < n)
        ++low;

    return static_cast<int>(low);
}

// ============================================================
// QCPAbstractPlottable 接口
// ============================================================

double QCPChunkedGraph::selectTest(const QPointF& pos, bool onlySelectable, QVariant* details) const
{
    if ((onlySelectable && mSelectable == QCP::stNone) || !m_keyCol || m_keyCol->empty())
        return -1;
    if (!mKeyAxis || !mValueAxis)
        return -1;

    QCPDataSelection selectionResult;
    double minDistSqr = std::numeric_limits<double>::max();
    int minDistIndex = static_cast<int>(m_keyCol->size());

    // 使用 findBegin/findEnd 缩小搜索范围
    double posKeyMin, posKeyMax, dummy;
    pixelsToCoords(pos - QPointF(mParentPlot->selectionTolerance(), mParentPlot->selectionTolerance()),
                   posKeyMin, dummy);
    pixelsToCoords(pos + QPointF(mParentPlot->selectionTolerance(), mParentPlot->selectionTolerance()),
                   posKeyMax, dummy);
    if (posKeyMin > posKeyMax)
        std::swap(posKeyMin, posKeyMax);

    int begin = findBegin(posKeyMin, true);
    int end = findEnd(posKeyMax, true);
    if (begin >= end)
        return -1;

    QCPRange keyRange(mKeyAxis->range());
    QCPRange valueRange(mValueAxis->range());
    const size_t n = static_cast<size_t>(end);

    for (int i = begin; i < end; ++i)
    {
        double k = m_keyCol->getDouble(static_cast<size_t>(i));
        double v = m_valueCol->getDouble(static_cast<size_t>(i));
        if (keyRange.contains(k) && valueRange.contains(v))
        {
            double currentDistSqr = QCPVector2D(coordsToPixels(k, v) - pos).lengthSquared();
            if (currentDistSqr < minDistSqr)
            {
                minDistSqr = currentDistSqr;
                minDistIndex = i;
            }
        }
    }

    if (minDistIndex != static_cast<int>(m_keyCol->size()))
        selectionResult.addDataRange(QCPDataRange(minDistIndex, minDistIndex + 1), false);

    selectionResult.simplify();
    if (details)
        details->setValue(selectionResult);

    return std::sqrt(minDistSqr);
}

QCPRange QCPChunkedGraph::getKeyRange(bool& foundRange, QCP::SignDomain inSignDomain) const
{
    foundRange = false;
    QCPRange range;

    if (!m_keyCol || m_keyCol->empty())
        return range;

    const size_t n = m_keyCol->size();

    double minVal = std::numeric_limits<double>::max();
    double maxVal = -std::numeric_limits<double>::max();
    bool hasValid = false;

    for (size_t i = 0; i < n; ++i)
    {
        double v = m_keyCol->getDouble(i);
        if (std::isnan(v)) continue;

        // 根据 signDomain 筛选
        if (inSignDomain == QCP::sdNegative && v > 0) continue;
        if (inSignDomain == QCP::sdPositive && v < 0) continue;

        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
        hasValid = true;
    }

    if (hasValid)
    {
        range.lower = minVal;
        range.upper = maxVal;
        foundRange = true;
    }

    return range;
}

QCPRange QCPChunkedGraph::getValueRange(bool& foundRange, QCP::SignDomain inSignDomain,
                                        const QCPRange& inKeyRange) const
{
    foundRange = false;
    QCPRange range;

    if (!m_valueCol || m_valueCol->empty())
        return range;

    const size_t n = m_valueCol->size();
    const bool restrictKeyRange = inKeyRange != QCPRange();

    double minVal = std::numeric_limits<double>::max();
    double maxVal = -std::numeric_limits<double>::max();
    bool hasValid = false;

    for (size_t i = 0; i < n; ++i)
    {
        if (restrictKeyRange && m_keyCol)
        {
            double k = m_keyCol->getDouble(i);
            if (k < inKeyRange.lower || k > inKeyRange.upper)
                continue;
        }

        double v = m_valueCol->getDouble(i);
        if (std::isnan(v)) continue;

        if (inSignDomain == QCP::sdNegative && v > 0) continue;
        if (inSignDomain == QCP::sdPositive && v < 0) continue;

        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
        hasValid = true;
    }

    if (hasValid)
    {
        range.lower = minVal;
        range.upper = maxVal;
        foundRange = true;
    }

    return range;
}

// ============================================================
// 绘图
// ============================================================

void QCPChunkedGraph::draw(QCPPainter* painter)
{
    if (!m_keyCol || !m_valueCol || m_keyCol->empty())
        return;

    // 获取可见数据范围
    QPair<int, int> visibleRange = getVisibleDataRange();
    if (visibleRange.first >= visibleRange.second)
        return;

    QCPDataRange dataRange(visibleRange.first, visibleRange.second);

    // 计算线条和散点
    QVector<QPointF> lines;
    QVector<QPointF> scatters;

    if (mLineStyle != lsNone)
        getLines(&lines, dataRange);

    if (mScatterStyle.shape() != QCPScatterStyle::ssNone)
        getScatters(&scatters, dataRange);

    // 绘制填充
    if (mBrush.style() != Qt::NoBrush)
    {
        applyFillAntialiasingHint(painter);
        painter->setBrush(mBrush);
        painter->setPen(Qt::NoPen);

        if (!lines.isEmpty())
        {
            // 构建填充多边形（到 y=0 基线）
            QPolygonF fillPolygon;
            fillPolygon.reserve(lines.size() + 2);

            fillPolygon << lines.first();
            for (const auto& pt : lines)
                fillPolygon << pt;

            // 回到基线
            QPointF lastPoint = lines.last();
            if (mValueAxis)
                fillPolygon << QPointF(lastPoint.x(), mValueAxis->coordToPixel(0));
            QPointF firstPoint = lines.first();
            fillPolygon << QPointF(firstPoint.x(), mValueAxis->coordToPixel(0));

            painter->drawPolygon(fillPolygon);
        }
    }

    // 绘制线
    if (mLineStyle != lsNone)
    {
        applyDefaultAntialiasingHint(painter);
        painter->setPen(mPen);
        painter->setBrush(Qt::NoBrush);
        drawLinePlot(painter, lines);
    }

    // 绘制散点
    if (mScatterStyle.shape() != QCPScatterStyle::ssNone)
    {
        applyScattersAntialiasingHint(painter);
        drawScatterPlot(painter, scatters);
    }
}

void QCPChunkedGraph::drawLegendIcon(QCPPainter* painter, const QRectF& rect) const
{
    // 画一段短的线+散点作为图例图标
    if (mLineStyle != lsNone)
    {
        painter->setPen(mPen);
        painter->drawLine(QPointF(rect.left(), rect.center().y()),
                          QPointF(rect.right(), rect.center().y()));
    }
    if (mScatterStyle.shape() != QCPScatterStyle::ssNone)
    {
        painter->setPen(mPen);
        painter->setBrush(Qt::NoBrush);
        mScatterStyle.drawShape(painter, rect.center());
    }
}

// ============================================================
// 绘图辅助
// ============================================================

QPair<int, int> QCPChunkedGraph::getVisibleDataRange() const
{
    if (!m_keyCol || !mKeyAxis)
        return {0, 0};

    const QCPRange axisRange = mKeyAxis->range();
    const int n = static_cast<int>(m_keyCol->size());

    int begin = findBegin(axisRange.lower, false);
    int end = findEnd(axisRange.upper, false);

    // 扩展一点边界，保证看到完整线条
    if (begin > 0) --begin;
    if (end < n) ++end;

    return {begin, end};
}

QVector<QCPGraphData> QCPChunkedGraph::fetchDataRange(int begin, int end) const
{
    QVector<QCPGraphData> result;
    if (!m_keyCol || !m_valueCol)
        return result;

    size_t count = static_cast<size_t>(end - begin);
    if (count == 0) return result;

    result.reserve(static_cast<int>(count));
    for (int i = begin; i < end; ++i)
    {
        size_t idx = static_cast<size_t>(i);
        double k = m_keyCol->getDouble(idx);
        double v = m_valueCol->getDouble(idx);

        // 过滤 NaN 值
        if (std::isnan(k) || std::isnan(v))
            continue;

        result.append(QCPGraphData(k, v));
    }

    return result;
}

void QCPChunkedGraph::getLines(QVector<QPointF>* lines, const QCPDataRange& dataRange) const
{
    if (!m_keyCol || !m_valueCol)
        return;

    // 获取范围内的数据
    QVector<QCPGraphData> data = fetchDataRange(
        dataRange.begin(), dataRange.end());

    if (data.isEmpty())
        return;

    // 根据线型转换
    switch (mLineStyle)
    {
    case lsLine:        *lines = dataToLines(data); break;
    case lsStepLeft:    *lines = dataToStepLeftLines(data); break;
    case lsStepRight:   *lines = dataToStepRightLines(data); break;
    case lsStepCenter:  *lines = dataToStepCenterLines(data); break;
    case lsImpulse:     *lines = dataToImpulseLines(data); break;
    default:            break;
    }
}

void QCPChunkedGraph::getScatters(QVector<QPointF>* scatters, const QCPDataRange& dataRange) const
{
    if (!m_keyCol || !m_valueCol)
        return;

    scatters->reserve(dataRange.size());

    for (int i = dataRange.begin(); i < dataRange.end(); ++i)
    {
        size_t idx = static_cast<size_t>(i);
        double k = m_keyCol->getDouble(idx);
        double v = m_valueCol->getDouble(idx);

        if (std::isnan(k) || std::isnan(v))
            continue;

        scatters->append(coordsToPixels(k, v));
    }
}

void QCPChunkedGraph::drawLinePlot(QCPPainter* painter, const QVector<QPointF>& lines) const
{
    if (lines.size() < 2)
        return;

    // 使用 QCPAbstractPlottable1D 的 drawPolyline 逻辑
    int i = 0;
    bool lastIsNan = false;
    const int lineDataSize = lines.size();
    while (i < lineDataSize && (std::isnan(lines.at(i).y()) || std::isnan(lines.at(i).x())))
        ++i;
    ++i;
    while (i < lineDataSize)
    {
        if (!std::isnan(lines.at(i).y()) && !std::isnan(lines.at(i).x()))
        {
            if (!lastIsNan)
                painter->drawLine(lines.at(i - 1), lines.at(i));
            else
                lastIsNan = false;
        }
        else
        {
            lastIsNan = true;
        }
        ++i;
    }
}

void QCPChunkedGraph::drawScatterPlot(QCPPainter* painter, const QVector<QPointF>& scatters) const
{
    for (const auto& pt : scatters)
        mScatterStyle.drawShape(painter, pt);
}

// ============================================================
// 坐标转换函数（来自 QCPGraph 的实现逻辑）
// ============================================================

QVector<QPointF> QCPChunkedGraph::dataToLines(const QVector<QCPGraphData>& data) const
{
    QVector<QPointF> result;
    result.reserve(data.size());
    for (const auto& d : data)
        result.append(coordsToPixels(d.key, d.value));
    return result;
}

QVector<QPointF> QCPChunkedGraph::dataToStepLeftLines(const QVector<QCPGraphData>& data) const
{
    QVector<QPointF> result;
    result.reserve(data.size() * 2);
    for (int i = 0; i < data.size(); ++i)
    {
        double key = data.at(i).key;
        double value = data.at(i).value;
        if (i > 0)
            result.append(coordsToPixels(key, data.at(i - 1).value));
        result.append(coordsToPixels(key, value));
    }
    return result;
}

QVector<QPointF> QCPChunkedGraph::dataToStepRightLines(const QVector<QCPGraphData>& data) const
{
    QVector<QPointF> result;
    result.reserve(data.size() * 2);
    for (int i = 0; i < data.size(); ++i)
    {
        double key = data.at(i).key;
        double value = data.at(i).value;
        result.append(coordsToPixels(key, value));
        if (i < data.size() - 1)
            result.append(coordsToPixels(data.at(i + 1).key, value));
    }
    return result;
}

QVector<QPointF> QCPChunkedGraph::dataToStepCenterLines(const QVector<QCPGraphData>& data) const
{
    QVector<QPointF> result;
    result.reserve(data.size() * 2);
    for (int i = 0; i < data.size(); ++i)
    {
        double key = data.at(i).key;
        double value = data.at(i).value;
        double centerKey;
        if (i == 0)
            centerKey = key;
        else if (i == data.size() - 1)
            centerKey = key;
        else
            centerKey = (key + data.at(i + 1).key) * 0.5;

        if (i > 0)
            result.append(coordsToPixels(centerKey, data.at(i - 1).value));
        result.append(coordsToPixels(centerKey, value));
    }
    return result;
}

QVector<QPointF> QCPChunkedGraph::dataToImpulseLines(const QVector<QCPGraphData>& data) const
{
    QVector<QPointF> result;
    result.reserve(data.size() * 2);
    for (const auto& d : data)
    {
        QPointF base = coordsToPixels(d.key, 0);
        QPointF tip = coordsToPixels(d.key, d.value);
        result.append(base);
        result.append(tip);
    }
    return result;
}

} // namespace viewer