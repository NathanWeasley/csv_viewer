#pragma once

#include "code_viewer/base/base_def.h"
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <utility>

namespace viewer
{

// ============================================================
// Column: 连续内存存储列（double）
// ============================================================
class Column
{
public:
    // ---------- const_iterator ----------
    class const_iterator
    {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = double;
        using difference_type   = ptrdiff_t;
        using pointer           = const double*;
        using reference         = const double&;

        const_iterator() noexcept = default;

        reference operator*()  const noexcept { return *m_ptr; }
        pointer   operator->() const noexcept { return m_ptr; }

        const_iterator& operator++()    noexcept { ++m_ptr; return *this; }
        const_iterator  operator++(int) noexcept { auto tmp = *this; ++(*this); return tmp; }
        const_iterator& operator--()    noexcept { --m_ptr; return *this; }
        const_iterator  operator--(int) noexcept { auto tmp = *this; --(*this); return tmp; }

        const_iterator& operator+=(difference_type n) noexcept { m_ptr += n; return *this; }
        const_iterator& operator-=(difference_type n) noexcept { m_ptr -= n; return *this; }

        reference operator[](difference_type n) const noexcept { return m_ptr[n]; }

        difference_type operator-(const const_iterator& other) const noexcept
        { return m_ptr - other.m_ptr; }

        bool operator==(const const_iterator& other) const noexcept { return m_ptr == other.m_ptr; }
        bool operator!=(const const_iterator& other) const noexcept { return m_ptr != other.m_ptr; }
        bool operator< (const const_iterator& other) const noexcept { return m_ptr <  other.m_ptr; }
        bool operator<=(const const_iterator& other) const noexcept { return m_ptr <= other.m_ptr; }
        bool operator> (const const_iterator& other) const noexcept { return m_ptr >  other.m_ptr; }
        bool operator>=(const const_iterator& other) const noexcept { return m_ptr >= other.m_ptr; }

    private:
        friend class Column;

        const double* m_ptr = nullptr;

        explicit const_iterator(const double* ptr) noexcept : m_ptr(ptr) {}
    };

    // ---------- iterator ----------
    class iterator
    {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = double;
        using difference_type   = ptrdiff_t;
        using pointer           = double*;
        using reference         = double&;

        iterator() noexcept = default;

        reference operator*()  const noexcept { return *m_ptr; }
        pointer   operator->() const noexcept { return m_ptr; }

        iterator& operator++()    noexcept { ++m_ptr; return *this; }
        iterator  operator++(int) noexcept { auto tmp = *this; ++(*this); return tmp; }
        iterator& operator--()    noexcept { --m_ptr; return *this; }
        iterator  operator--(int) noexcept { auto tmp = *this; --(*this); return tmp; }

        iterator& operator+=(difference_type n) noexcept { m_ptr += n; return *this; }
        iterator& operator-=(difference_type n) noexcept { m_ptr -= n; return *this; }

        reference operator[](difference_type n) const noexcept { return m_ptr[n]; }

        difference_type operator-(const iterator& other) const noexcept
        { return m_ptr - other.m_ptr; }

        bool operator==(const iterator& other) const noexcept { return m_ptr == other.m_ptr; }
        bool operator!=(const iterator& other) const noexcept { return m_ptr != other.m_ptr; }
        bool operator< (const iterator& other) const noexcept { return m_ptr <  other.m_ptr; }
        bool operator>=(const iterator& other) const noexcept { return m_ptr >= other.m_ptr; }

        // 隐式转换为 const_iterator
        operator const_iterator() const noexcept { return const_iterator(m_ptr); }

    private:
        friend class Column;

        double* m_ptr = nullptr;

        explicit iterator(double* ptr) noexcept : m_ptr(ptr) {}
    };

    // ---------- 构造 ----------
    Column() noexcept = default;
    explicit Column(size_t size)
        : m_data(size)
    {
    }
    explicit Column(std::vector<double>&& values) noexcept
        : m_data(std::move(values))
    {
    }
    ~Column() = default;

    // ---- 拷贝 ----
    Column(const Column&) = default;
    Column& operator=(const Column&) = default;

    // ---- 移动 ----
    Column(Column&& other) noexcept
        : m_data(std::move(other.m_data))
        , m_cachedMin(other.m_cachedMin)
        , m_cachedMax(other.m_cachedMax)
        , m_minMaxValid(other.m_minMaxValid)
    {
        other.m_minMaxValid = false;
    }
    Column& operator=(Column&& other) noexcept
    {
        if (this != &other)
        {
            m_data = std::move(other.m_data);
            m_cachedMin = other.m_cachedMin;
            m_cachedMax = other.m_cachedMax;
            m_minMaxValid = other.m_minMaxValid;
            other.m_minMaxValid = false;
        }
        return *this;
    }

    // -------- 容量 --------
    size_t size()     const noexcept { return m_data.size(); }
    bool   empty()    const noexcept { return m_data.empty(); }
    size_t capacity() const noexcept { return m_data.capacity(); }

    void clear()
    {
        std::vector<double>().swap(m_data);
        m_minMaxValid = false;
    }

    // 整列覆盖：一次性准备有效长度，随后通过 operator[] 写入，
    // 全部写完后调用 recalcMinMax()。容量足够时不会重新分配。
    void beginOverwrite(size_t size)
    {
        m_data.resize(size);
        m_minMaxValid = false;
    }

    void beginOverwrite() noexcept
    {
        m_minMaxValid = false;
    }

    // -------- 元素访问 --------
    double  operator[](size_t idx) const noexcept { return m_data[idx]; }
    double& operator[](size_t idx) noexcept       { return m_data[idx]; }

    double getDouble(size_t idx) const { return m_data[idx]; }

    const double& back() const noexcept
    {
        static const double s_empty = 0.0;
        return !m_data.empty() ? m_data.back() : s_empty;
    }

    double& back() noexcept { return m_data.back(); }

    // 裸指针访问（零开销内联遍历，用于绘图热路径）
    const double* data() const noexcept { return m_data.data(); }
    double*       data()       noexcept { return m_data.data(); }

    // -------- 修改 --------
    void push_back(double val)
    {
        if (m_data.size() == m_data.capacity())
            m_data.reserve(m_data.empty() ? 4096 : m_data.capacity() * 2);
        m_data.push_back(val);

        // 增量更新 min/max 缓存（O(1)）
        if (!std::isnan(val))
        {
            if (!m_minMaxValid)
            {
                m_cachedMin = val;
                m_cachedMax = val;
                m_minMaxValid = true;
            }
            else
            {
                if (val < m_cachedMin) m_cachedMin = val;
                if (val > m_cachedMax) m_cachedMax = val;
            }
        }
    }

    void pushFromString(const std::string& s)
    {
        char* end = nullptr;
        double val = std::strtod(s.c_str(), &end);

        if (end != s.c_str() && *end == '\0')
            push_back(val);
        else
            push_back(std::numeric_limits<double>::quiet_NaN());
    }

    void reserve(size_t cap)
    {
        m_data.reserve(cap);
    }

    // -------- 迭代器 --------
    const_iterator begin()  const noexcept { return const_iterator(m_data.data()); }
    const_iterator end()    const noexcept
    {
        const double* pointer = m_data.data();
        return const_iterator(pointer ? pointer + m_data.size() : pointer);
    }
    iterator       begin()        noexcept { return iterator(m_data.data()); }
    iterator       end()          noexcept
    {
        double* pointer = m_data.data();
        return iterator(pointer ? pointer + m_data.size() : pointer);
    }

    // 批量追加
    template <typename InputIt>
    void append(InputIt first, InputIt last)
    {
        for (; first != last; ++first)
            push_back(static_cast<double>(*first));
    }

    void append(const std::vector<double>& vec)
    {
        append(vec.begin(), vec.end());
    }

    // -------- 范围 min/max --------
    std::pair<double, double> rangeMinMax(size_t begin, size_t end) const
    {
        double vmin = std::numeric_limits<double>::max();
        double vmax = -std::numeric_limits<double>::max();

        for (size_t i = begin; i < end; ++i)
        {
            if (std::isnan(m_data[i]))
                continue;
            double v = m_data[i];
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
        }

        return { vmin, vmax };
    }

    // -------- 全列缓存 min/max --------
    double cachedMin() const noexcept { return m_cachedMin; }
    double cachedMax() const noexcept { return m_cachedMax; }
    bool   hasCachedMinMax() const noexcept { return m_minMaxValid; }

    void recalcMinMax()
    {
        m_minMaxValid = false;
        if (m_data.empty())
            return;

        double vmin = std::numeric_limits<double>::max();
        double vmax = -std::numeric_limits<double>::max();
        bool hasValid = false;

        for (size_t i = 0; i < m_data.size(); ++i)
        {
            if (std::isnan(m_data[i]))
                continue;
            double v = m_data[i];
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
            hasValid = true;
        }

        if (hasValid)
        {
            m_cachedMin = vmin;
            m_cachedMax = vmax;
            m_minMaxValid = true;
        }
    }

private:
    std::vector<double> m_data;

    // min/max 缓存（全列，加载/表达式后自动计算）
    double  m_cachedMin = 0.0;
    double  m_cachedMax = 0.0;
    bool    m_minMaxValid = false;
};

} // namespace viewer
