#include "code_viewer/plotmgr/graph/qcp_column_graph.h"
#include <limits>

namespace viewer
{

// 静态成员定义：默认启用自适应降采样
bool QCPColumnGraph::s_adaptiveSamplingEnabled = true;

// 静态成员定义：默认开启，由 UI 配置在创建曲线前设置
bool QCPColumnGraph::s_antiAliasingEnabled = true;

// ============================================================
// 构造 / 析构
// ============================================================
QCPColumnGraph::QCPColumnGraph(QCPAxis* keyAxis, QCPAxis* valueAxis)
    : QCPAbstractPlottable(keyAxis, valueAxis)
{
    mName = "ColumnGraph";
    // 默认散点样式：不显示散点
    mScatterStyle = QCPScatterStyle(QCPScatterStyle::ssNone);
    setAntialiased(s_antiAliasingEnabled);
    setAntialiasedScatters(s_antiAliasingEnabled);
}

QCPColumnGraph::~QCPColumnGraph()
{
    // 不拥有数据列指针，不需要释放
}

// ============================================================
// 设置数据列
// ============================================================
void QCPColumnGraph::setDataColumns(const Column* keyCol, const Column* valueCol)
{
    m_keyCol = keyCol;
    m_valueCol = valueCol;
    // 数据绑定只交换非拥有型指针。全列范围仅在 rescaleAxes 等调用真正需要时计算，
    // 普通重绘沿用当前坐标范围，避免提交派生列时同步扫描整列。
    invalidateRangeCache();
}

void QCPColumnGraph::setLineStyle(LineStyle style)
{
    mLineStyle = style;
}

void QCPColumnGraph::setScatterStyle(const QCPScatterStyle& style)
{
    mScatterStyle = style;
}

// ============================================================
// QCPPlottableInterface1D 接口
// ============================================================

int QCPColumnGraph::dataCount() const
{
    if (!m_valueCol) return 0;
    return static_cast<int>(m_valueCol->size());
}

double QCPColumnGraph::dataMainKey(int index) const
{
    if (m_keyCol) return m_keyCol->getDouble(static_cast<size_t>(index));
    return static_cast<double>(index);
}

double QCPColumnGraph::dataSortKey(int index) const
{
    // 对于普通线图，sort key = main key
    return dataMainKey(index);
}

double QCPColumnGraph::dataMainValue(int index) const
{
    if (!m_valueCol) return 0;
    return m_valueCol->getDouble(static_cast<size_t>(index));
}

QCPRange QCPColumnGraph::dataValueRange(int index) const
{
    double v = dataMainValue(index);
    return QCPRange(v, v);
}

QPointF QCPColumnGraph::dataPixelPosition(int index) const
{
    return coordsToPixels(dataMainKey(index), dataMainValue(index));
}

bool QCPColumnGraph::sortKeyIsMainKey() const
{
    return true;
}

QCPDataSelection QCPColumnGraph::selectTestRect(const QRectF& rect, bool onlySelectable) const
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

int QCPColumnGraph::findBegin(double sortKey, bool expandedRange) const
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

int QCPColumnGraph::findEnd(double sortKey, bool expandedRange) const
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

double QCPColumnGraph::selectTest(const QPointF& pos, bool onlySelectable, QVariant* details) const
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

    // 辅助函数：计算线段 (a, b) 到点 p 的平方距离
    auto segmentDistSqr = [](const QPointF& a, const QPointF& b, const QPointF& p) -> double
    {
        QPointF ab = b - a;
        double lenSqr = ab.x() * ab.x() + ab.y() * ab.y();
        if (lenSqr < 1e-12)
            return (p - a).x() * (p - a).x() + (p - a).y() * (p - a).y();  // 退化为两点
        double t = ((p - a).x() * ab.x() + (p - a).y() * ab.y()) / lenSqr;
        t = std::max(0.0, std::min(1.0, t));
        QPointF proj = a + t * ab;
        return (p - proj).x() * (p - proj).x() + (p - proj).y() * (p - proj).y();
    };

    const double tolPx = static_cast<double>(mParentPlot->selectionTolerance());

    for (int i = begin; i < end; ++i)
    {
        double k = m_keyCol->getDouble(static_cast<size_t>(i));
        double v = m_valueCol->getDouble(static_cast<size_t>(i));
        if (keyRange.contains(k) && valueRange.contains(v))
        {
            QPointF pt = coordsToPixels(k, v);

            // 点到当前数据点的距离
            double distSqr = QCPVector2D(pt - pos).lengthSquared();
            if (distSqr < minDistSqr)
            {
                minDistSqr = distSqr;
                minDistIndex = i;
            }

            // 点到前一段连线的距离（i-1 -> i）
            if (i > begin && mLineStyle != lsNone)
            {
                double pk = m_keyCol->getDouble(static_cast<size_t>(i - 1));
                double pv = m_valueCol->getDouble(static_cast<size_t>(i - 1));
                if (keyRange.contains(pk) && valueRange.contains(pv))
                {
                    QPointF prevPt = coordsToPixels(pk, pv);
                    double segDist = segmentDistSqr(prevPt, pt, pos);
                    if (segDist < minDistSqr && QCPVector2D(prevPt - pt).length() < tolPx * 2 + QCPVector2D(coordsToPixels(k - m_keyCol->getDouble(static_cast<size_t>(i - 1)), 0)).length())
                    {
                        minDistSqr = segDist;
                        minDistIndex = i;
                    }
                }
            }

            // 点到后一段连线的距离（i -> i+1）
            if (i + 1 < end && mLineStyle != lsNone)
            {
                double nk = m_keyCol->getDouble(static_cast<size_t>(i + 1));
                double nv = m_valueCol->getDouble(static_cast<size_t>(i + 1));
                if (keyRange.contains(nk) && valueRange.contains(nv))
                {
                    QPointF nextPt = coordsToPixels(nk, nv);
                    double segDist = segmentDistSqr(pt, nextPt, pos);
                    if (segDist < minDistSqr)
                    {
                        minDistSqr = segDist;
                        minDistIndex = i;
                    }
                }
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

QCPRange QCPColumnGraph::getKeyRange(bool& foundRange, QCP::SignDomain inSignDomain) const
{
    foundRange = false;
    QCPRange range;

    if (!m_keyCol || m_keyCol->empty())
        return range;

    // 无 signDomain 筛选且未限定 keyRange 时直接使用缓存
    if (inSignDomain == QCP::sdBoth)
    {
        if (!mRangeCacheValid)
            const_cast<QCPColumnGraph*>(this)->recalculateRanges();

        if (mRangeCacheValid)
        {
            range.lower = mCachedKeyMin;
            range.upper = mCachedKeyMax;
            foundRange = true;
            return range;
        }
    }

    // 有 signDomain 筛选时仍需要全量扫描（极少触发）
    const size_t n = m_keyCol->size();
    double minVal = std::numeric_limits<double>::max();
    double maxVal = -std::numeric_limits<double>::max();
    bool hasValid = false;

    for (size_t i = 0; i < n; ++i)
    {
        double v = m_keyCol->getDouble(i);
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

QCPRange QCPColumnGraph::getValueRange(bool& foundRange, QCP::SignDomain inSignDomain,
                                        const QCPRange& inKeyRange) const
{
    foundRange = false;
    QCPRange range;

    if (!m_valueCol || m_valueCol->empty())
        return range;

    const bool restrictKeyRange = inKeyRange != QCPRange();

    // 无 signDomain 筛选且未限定 keyRange 时直接使用缓存
    if (inSignDomain == QCP::sdBoth && !restrictKeyRange)
    {
        if (!mRangeCacheValid)
            const_cast<QCPColumnGraph*>(this)->recalculateRanges();

        if (mRangeCacheValid)
        {
            range.lower = mCachedValueMin;
            range.upper = mCachedValueMax;
            foundRange = true;
            return range;
        }
    }

    // 需要按 keyRange 限制或 signDomain 筛选时全量扫描
    const size_t n = m_valueCol->size();
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

void QCPColumnGraph::draw(QCPPainter* painter)
{
    if (!m_keyCol || !m_valueCol || m_keyCol->empty())
        return;

    ///< Total number of points can be displayed (csv rows)
    QPair<int, int> visibleRange = getVisibleDataRange();
    int begin = visibleRange.first;
    int end   = visibleRange.second;
    if (begin >= end)
        return;
    int visibleCount = end - begin;

    ///< Total available pixels on figure
    int pixelsW = screenPixelWidth();

    ///< point-to-pixel ratio
    double ratio = static_cast<double>(visibleCount) / static_cast<double>(std::max(pixelsW, 1));

    ///< Total available figure X-axis range (in x-variable unit IMPORTANT!)
    auto axisrange = mKeyAxis->range();
    double xspan = axisrange.upper - axisrange.lower;

    ///< Maximum available X-axis range
    double xdatarange;
    if (mRangeCacheValid)
    {
        xdatarange = mCachedKeyMax - mCachedKeyMin;
    }
    else if (m_keyCol->hasCachedMinMax())
    {
        xdatarange = m_keyCol->cachedMax() - m_keyCol->cachedMin();
    }
    else
    {
        auto r = m_keyCol->rangeMinMax(begin, end);
        xdatarange = r.second - r.first;
    }

    ///< rescale ratio to account for span < width case
    double bucket_scale = 1.0;
    if (std::abs(xdatarange) > 1e-12)
        bucket_scale = std::max(1.0, xspan / xdatarange);

    bool useDownsample = (s_adaptiveSamplingEnabled && ratio > 2.0 && mLineStyle == lsLine);

    // 线和散点都保持用户显式的抗锯齿设置，不因降采样自动打开。
    const bool useLineAA = s_antiAliasingEnabled;
    const bool useScatterAA = s_antiAliasingEnabled;

    int buckets = std::max(1, static_cast<int>(std::ceil(pixelsW * std::min(ratio, 1.0)/bucket_scale)));
    if (buckets > visibleCount / 2)
        buckets = std::max(1, visibleCount / 2);

    m_lineBuffer.clear();
    m_scatterBuffer.clear();

    if (mLineStyle != lsNone)
    {
        if (useDownsample)
            getLinesDownsampled(&m_lineBuffer, begin, end, buckets);
        else
            getLinesDirectStyled(&m_lineBuffer, begin, end);
    }

    if (mScatterStyle.shape() != QCPScatterStyle::ssNone)
    {
        QCPDataRange dataRange(begin, end);
        getScatters(&m_scatterBuffer, dataRange);
    }

    if (mBrush.style() != Qt::NoBrush && !m_lineBuffer.isEmpty())
    {
        applyFillAntialiasingHint(painter);
        painter->setBrush(mBrush);
        painter->setPen(Qt::NoPen);

        m_fillPolygonBuffer.clear();
        m_fillPolygonBuffer.reserve(m_lineBuffer.size() + 2);
        m_fillPolygonBuffer << m_lineBuffer.first();
        for (const auto& pt : m_lineBuffer)
            m_fillPolygonBuffer << pt;
        QPointF lastPoint = m_lineBuffer.last();
        if (mValueAxis)
            m_fillPolygonBuffer << QPointF(lastPoint.x(), mValueAxis->coordToPixel(0));
        m_fillPolygonBuffer << QPointF(m_lineBuffer.first().x(), mValueAxis->coordToPixel(0));
        painter->drawPolygon(m_fillPolygonBuffer);
    }

    if (mLineStyle != lsNone)
    {
        applyAntialiasingHint(painter, useLineAA, QCP::aePlottables);
        painter->setPen(mPen);
        painter->setBrush(Qt::NoBrush);
        drawLinePlot(painter, m_lineBuffer);
    }

    if (mScatterStyle.shape() != QCPScatterStyle::ssNone)
    {
        applyAntialiasingHint(painter, useScatterAA, QCP::aeScatters);
        drawScatterPlot(painter, m_scatterBuffer);
    }
}

void QCPColumnGraph::drawLegendIcon(QCPPainter* painter, const QRectF& rect) const
{
    // 构建自定义 dash pattern
    QPen iconPen = mPen;
    switch (mPen.style())
    {
    case Qt::DotLine:
        iconPen.setStyle(Qt::CustomDashLine);
        iconPen.setDashPattern({1.0, 4.0});
        break;
    case Qt::DashLine:
        iconPen.setStyle(Qt::CustomDashLine);
        iconPen.setDashPattern({6.0, 4.0});
        break;
    case Qt::DashDotLine:
        iconPen.setStyle(Qt::CustomDashLine);
        iconPen.setDashPattern({6.0, 4.0, 1.0, 4.0});
        break;
    default:
        break;
    }

    // 画一段短的线+散点作为图例图标
    if (mLineStyle != lsNone)
    {
        painter->setPen(iconPen);
        painter->drawLine(QPointF(rect.left(), rect.center().y()),
                          QPointF(rect.right(), rect.center().y()));
    }
    if (mScatterStyle.shape() != QCPScatterStyle::ssNone)
    {
        painter->setBrush(Qt::NoBrush);
        mScatterStyle.drawShape(painter, rect.center());
    }
}

// ============================================================
// 绘图辅助
// ============================================================

QPair<int, int> QCPColumnGraph::getVisibleDataRange() const
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

QVector<QCPGraphData> QCPColumnGraph::fetchDataRange(int begin, int end) const
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

void QCPColumnGraph::getLines(QVector<QPointF>* lines, const QCPDataRange& dataRange) const
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

void QCPColumnGraph::getScatters(QVector<QPointF>* scatters, const QCPDataRange& dataRange) const
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

void QCPColumnGraph::drawLinePlot(QCPPainter* painter, const QVector<QPointF>& lines) const
{
    if (lines.size() < 2)
        return;

    // 构建自定义 dash pattern（使虚线/点线的间隔随线宽自适应）
    QPen drawPen = mPen;
    const double w = static_cast<double>(mPen.widthF());
    switch (mPen.style())
    {
    case Qt::DotLine:
        drawPen.setStyle(Qt::CustomDashLine);
        drawPen.setDashPattern({1.0, 4.0});  // 点: 1×w 实 + 4×w 空
        break;
    case Qt::DashLine:
        drawPen.setStyle(Qt::CustomDashLine);
        drawPen.setDashPattern({6.0, 4.0});  // 虚线: 6×w 实 + 4×w 空
        break;
    case Qt::DashDotLine:
        drawPen.setStyle(Qt::CustomDashLine);
        drawPen.setDashPattern({6.0, 4.0, 1.0, 4.0});  // 点划线: 6+1 实 / 4+4 空
        break;
    default:
        break;
    }

    // 使用 QPainter::drawLines() 批量绘制相邻有效线段
    m_segmentBuffer.clear();
    m_segmentBuffer.reserve(lines.size());

    for (int i = 1; i < lines.size(); ++i)
    {
        const auto& p0 = lines.at(i - 1);
        const auto& p1 = lines.at(i);
        if (!std::isnan(p0.y()) && !std::isnan(p1.y()) &&
            !std::isnan(p0.x()) && !std::isnan(p1.x()))
        {
            m_segmentBuffer.append(QLineF(p0, p1));
        }
    }

    if (!m_segmentBuffer.isEmpty())
    {
        painter->setPen(drawPen);
        painter->drawLines(m_segmentBuffer);
        painter->setPen(mPen);  // 恢复原 pen
    }
}

void QCPColumnGraph::drawScatterPlot(QCPPainter* painter, const QVector<QPointF>& scatters) const
{
    if (scatters.isEmpty())
        return;

    // QCPScatterStyle::drawShape relies on the painter pen/brush already being set.
    mScatterStyle.applyTo(painter, mPen);

    const auto shape = mScatterStyle.shape();
    const double size = mScatterStyle.size();
    const double half = size * 0.5;

    if ((shape == QCPScatterStyle::ssCircle || shape == QCPScatterStyle::ssDisc) && size > 0.0)
    {
        // 分批复用 path 缓冲，避免拖拽/缩放时为整批散点反复申请大块内存。
        constexpr int kScatterPathBatchSize = 256;
        for (int start = 0; start < scatters.size(); start += kScatterPathBatchSize)
        {
            const int end = std::min(start + kScatterPathBatchSize, static_cast<int>(scatters.size()));
            m_scatterPathBuffer.clear();
            m_scatterPathBuffer.reserve((end - start) * 16);
            for (int i = start; i < end; ++i)
                m_scatterPathBuffer.addEllipse(scatters.at(i), half, half);
            painter->drawPath(m_scatterPathBuffer);
        }
        return;
    }

    if (shape == QCPScatterStyle::ssSquare && size > 0.0)
    {
        m_scatterRectBuffer.clear();
        m_scatterRectBuffer.reserve(scatters.size());
        for (const auto& pt : scatters)
            m_scatterRectBuffer.append(QRectF(pt.x() - half, pt.y() - half, size, size));
        painter->drawRects(m_scatterRectBuffer.constData(), m_scatterRectBuffer.size());
        return;
    }

    for (const auto& pt : scatters)
        mScatterStyle.drawShape(painter, pt);
}

// ============================================================
// 坐标转换函数（来自 QCPGraph 的实现逻辑）
// ============================================================

QVector<QPointF> QCPColumnGraph::dataToLines(const QVector<QCPGraphData>& data) const
{
    QVector<QPointF> result;
    result.reserve(data.size());
    for (const auto& d : data)
        result.append(coordsToPixels(d.key, d.value));
    return result;
}

QVector<QPointF> QCPColumnGraph::dataToStepLeftLines(const QVector<QCPGraphData>& data) const
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

QVector<QPointF> QCPColumnGraph::dataToStepRightLines(const QVector<QCPGraphData>& data) const
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

QVector<QPointF> QCPColumnGraph::dataToStepCenterLines(const QVector<QCPGraphData>& data) const
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

QVector<QPointF> QCPColumnGraph::dataToImpulseLines(const QVector<QCPGraphData>& data) const
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

// ============================================================
// 范围缓存
// ============================================================

void QCPColumnGraph::recalculateRanges()
{
    mRangeCacheValid = false;

    if (!m_keyCol || m_keyCol->empty() || !m_valueCol || m_valueCol->empty())
        return;

    const size_t ksz = m_keyCol->size();
    const size_t vsz = m_valueCol->size();

    // --- key range ---
    double kMin = std::numeric_limits<double>::max();
    double kMax = -std::numeric_limits<double>::max();
    bool kValid = false;

    for (size_t i = 0; i < ksz; ++i)
    {
        double v = m_keyCol->getDouble(i);
        if (std::isnan(v)) continue;
        if (v < kMin) kMin = v;
        if (v > kMax) kMax = v;
        kValid = true;
    }

    // --- value range ---
    double vMin = std::numeric_limits<double>::max();
    double vMax = -std::numeric_limits<double>::max();
    bool vValid = false;

    for (size_t i = 0; i < vsz; ++i)
    {
        double v = m_valueCol->getDouble(i);
        if (std::isnan(v)) continue;
        if (v < vMin) vMin = v;
        if (v > vMax) vMax = v;
        vValid = true;
    }

    if (kValid && vValid)
    {
        mCachedKeyMin   = kMin;
        mCachedKeyMax   = kMax;
        mCachedValueMin = vMin;
        mCachedValueMax = vMax;
        mRangeCacheValid = true;
    }
}

void QCPColumnGraph::invalidateRangeCache()
{
    mRangeCacheValid = false;
}

// ============================================================
// Column → QPointF 直出（消除 QCPGraphData 中间拷贝）
// ============================================================

void QCPColumnGraph::getLinesDirect(QVector<QPointF>* lines, int begin, int end) const
{
    if (!m_keyCol || !m_valueCol)
        return;

    int count = end - begin;
    if (count <= 0)
        return;

    lines->reserve(count);

    const double* kPtr = m_keyCol->data() + begin;
    const double* vPtr = m_valueCol->data() + begin;

    for (int i = 0; i < count; ++i)
    {
        double k = kPtr[i];
        double v = vPtr[i];
        if (!std::isnan(k) && !std::isnan(v))
            lines->append(coordsToPixels(k, v));
    }
}

void QCPColumnGraph::getLinesDirectStyled(QVector<QPointF>* lines, int begin, int end) const
{
    if (!m_keyCol || !m_valueCol)
        return;

    if (mLineStyle == lsLine)
    {
        getLinesDirect(lines, begin, end);
        return;
    }

    QCPDataRange dr(begin, end);
    getLines(lines, dr);
}

// ============================================================
// 元素级降采样
// ============================================================

void QCPColumnGraph::getLinesDownsampled(QVector<QPointF>* lines,
                                           int begin, int end, int numBuckets) const
{
    if (!m_keyCol || !m_valueCol || numBuckets <= 0)
        return;

    int totalCount = end - begin;
    if (totalCount <= 0)
        return;

    if (numBuckets > totalCount)
        numBuckets = totalCount;

    lines->reserve(numBuckets * 2);

    const size_t stotal = static_cast<size_t>(totalCount);
    const size_t snum  = static_cast<size_t>(numBuckets);

    for (size_t b = 0; b < snum; ++b)
    {
        int bucketBegin = begin + static_cast<int>((stotal * b) / snum);
        int bucketEnd   = begin + static_cast<int>((stotal * (b + 1)) / snum);
        if (bucketEnd <= bucketBegin)
            bucketEnd = bucketBegin + 1;

        double yMin = std::numeric_limits<double>::max();
        double yMax = -std::numeric_limits<double>::max();
        double xSum = 0.0;
        int validCount = 0;

        const double* kPtr = m_keyCol->data() + bucketBegin;
        const double* vPtr = m_valueCol->data() + bucketBegin;
        int bucketSize = bucketEnd - bucketBegin;

        for (int i = 0; i < bucketSize; ++i)
        {
            double k = kPtr[i];
            double v = vPtr[i];
            if (std::isnan(k) || std::isnan(v))
                continue;

            if (v < yMin) yMin = v;
            if (v > yMax) yMax = v;
            xSum += k;
            ++validCount;
        }

        if (validCount == 0)
        {
            lines->append(QPointF(NAN, NAN));
            lines->append(QPointF(NAN, NAN));
            continue;
        }

        double centerX = xSum / static_cast<double>(validCount);
        lines->append(coordsToPixels(centerX, yMin));
        lines->append(coordsToPixels(centerX, yMax));
    }
}

// ============================================================
// 工具
// ============================================================

int QCPColumnGraph::screenPixelWidth() const
{
    if (mKeyAxis && mKeyAxis->axisRect())
        return mKeyAxis->axisRect()->width();
    return 800;
}

} // namespace viewer
