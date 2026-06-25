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
    m_keyCol = static_cast<const Column<double>*>(keyCol);
    m_valueCol = static_cast<const Column<double>*>(valueCol);
    recalculateRanges();
}

void QCPChunkedGraph::setLineStyle(LineStyle style)
{
    mLineStyle = style;
    if (mParentPlot)
        mParentPlot->replot();
}

void QCPChunkedGraph::setScatterStyle(const QCPScatterStyle& style)
{
    mScatterStyle = style;
    if (mParentPlot)
        mParentPlot->replot();
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

    // 无 signDomain 筛选且未限定 keyRange 时直接使用缓存
    if (inSignDomain == QCP::sdBoth)
    {
        if (!mRangeCacheValid)
            const_cast<QCPChunkedGraph*>(this)->recalculateRanges();

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

QCPRange QCPChunkedGraph::getValueRange(bool& foundRange, QCP::SignDomain inSignDomain,
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
            const_cast<QCPChunkedGraph*>(this)->recalculateRanges();

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

void QCPChunkedGraph::draw(QCPPainter* painter)
{
    if (!m_keyCol || !m_valueCol || m_keyCol->empty())
        return;

    QPair<int, int> visibleRange = getVisibleDataRange();
    int begin = visibleRange.first;
    int end   = visibleRange.second;
    if (begin >= end)
        return;

    int visibleCount = end - begin;

    int pixelsW = screenPixelWidth();
    double ratio = static_cast<double>(visibleCount) / static_cast<double>(std::max(pixelsW, 1));
    bool useDownsample = (ratio > 2.0 && mLineStyle == lsLine);

    int buckets = std::max(1, static_cast<int>(pixelsW * std::min(ratio, 2.0)));
    if (buckets > visibleCount / 2)
        buckets = std::max(1, visibleCount / 2);

    QVector<QPointF> lines;
    QVector<QPointF> scatters;

    if (mLineStyle != lsNone)
    {
        if (useDownsample)
            getLinesDownsampled(&lines, begin, end, buckets);
        else
            getLinesDirectStyled(&lines, begin, end);
    }

    if (mScatterStyle.shape() != QCPScatterStyle::ssNone)
    {
        QCPDataRange dataRange(begin, end);
        if (useDownsample)
            getLinesDownsampled(&scatters, begin, end, buckets);
        else
            getScatters(&scatters, dataRange);
    }

    if (mBrush.style() != Qt::NoBrush && !lines.isEmpty())
    {
        applyFillAntialiasingHint(painter);
        painter->setBrush(mBrush);
        painter->setPen(Qt::NoPen);

        QPolygonF fillPolygon;
        fillPolygon.reserve(lines.size() + 2);
        fillPolygon << lines.first();
        for (const auto& pt : lines)
            fillPolygon << pt;
        QPointF lastPoint = lines.last();
        if (mValueAxis)
            fillPolygon << QPointF(lastPoint.x(), mValueAxis->coordToPixel(0));
        fillPolygon << QPointF(lines.first().x(), mValueAxis->coordToPixel(0));
        painter->drawPolygon(fillPolygon);
    }

    if (mLineStyle != lsNone)
    {
        applyDefaultAntialiasingHint(painter);
        painter->setPen(mPen);
        painter->setBrush(Qt::NoBrush);
        drawLinePlot(painter, lines);
    }

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

    // 使用 QPainter::drawLines() 批量绘制相邻有效线段
    QVector<QLineF> segments;
    segments.reserve(lines.size());

    for (int i = 1; i < lines.size(); ++i)
    {
        const auto& p0 = lines.at(i - 1);
        const auto& p1 = lines.at(i);
        if (!std::isnan(p0.y()) && !std::isnan(p1.y()) &&
            !std::isnan(p0.x()) && !std::isnan(p1.x()))
        {
            segments.append(QLineF(p0, p1));
        }
    }

    if (!segments.isEmpty())
        painter->drawLines(segments);
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

// ============================================================
// 范围缓存
// ============================================================

void QCPChunkedGraph::recalculateRanges()
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

void QCPChunkedGraph::invalidateRangeCache()
{
    mRangeCacheValid = false;
}

// ============================================================
// Column → QPointF 直出（消除 QCPGraphData 中间拷贝）
// ============================================================

void QCPChunkedGraph::getLinesDirect(QVector<QPointF>* lines, int begin, int end) const
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

void QCPChunkedGraph::getLinesDirectStyled(QVector<QPointF>* lines, int begin, int end) const
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

void QCPChunkedGraph::getLinesDownsampled(QVector<QPointF>* lines,
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

int QCPChunkedGraph::screenPixelWidth() const
{
    if (mKeyAxis && mKeyAxis->axisRect())
        return mKeyAxis->axisRect()->width();
    return 800;
}

} // namespace viewer