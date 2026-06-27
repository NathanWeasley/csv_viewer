#pragma once

#include "code_viewer/base/base_def.h"
#include <memory>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <climits>
#include <stdexcept>
#include <type_traits>

namespace viewer
{

// ============================================================
// ColumnType: 列类型标记
// ============================================================
enum class ColumnType : uint8_t
{
    Float64     // 所有数据统一以 double 存储
};

// ============================================================
// CellType: 单元格分类（用于类型推断阶段）
// ============================================================
enum class CellType : uint8_t
{
    Float,      // 可解析为浮点数
    String      // 不可解析的字符串
};

// ============================================================
// 类型辅助函数
// ============================================================

// 分类一个单元格字符串属于什么类型
inline CellType classifyCell(const std::string& s)
{
    if (s.empty())
        return CellType::Float;

    char* end = nullptr;
    std::strtod(s.c_str(), &end);
    if (end != s.c_str() && *end == '\0')
        return CellType::Float;

    return CellType::String;
}

// ============================================================
// AbstractColumn: 多态基类
// ============================================================
struct AbstractColumn
{
    virtual ~AbstractColumn() = default;

    virtual ColumnType  type() const noexcept = 0;
    virtual size_t      size() const noexcept = 0;
    virtual bool        empty() const noexcept = 0;
    virtual void        pushFromString(const std::string& s) = 0;
    virtual double      getDouble(size_t idx) const = 0;
    virtual std::string typeName() const = 0;
    virtual void        clear() = 0;

    // 查询 [begin, end) 范围内的 min/max（降采样用）
    virtual std::pair<double, double> rangeMinMax(size_t begin, size_t end) const = 0;

    // 全列缓存的 min/max（加载/表达式计算后自动填充）
    virtual double      cachedMin() const noexcept = 0;
    virtual double      cachedMax() const noexcept = 0;
    virtual bool        hasCachedMinMax() const noexcept = 0;
    virtual void        recalcMinMax() = 0;
};

// ============================================================
// Column<T>: 连续内存存储列模板（仅支持 double）
// ============================================================
template <typename T>
class Column
    : public AbstractColumn
{
    static_assert(std::is_same<T, double>::value,
                  "Column<T> only supports double");

public:
    // ---------- const_iterator ----------
    class const_iterator
    {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = T;
        using difference_type   = ptrdiff_t;
        using pointer           = const T*;
        using reference         = const T&;

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
        friend class Column<T>;

        const T* m_ptr = nullptr;

        explicit const_iterator(const T* ptr) noexcept : m_ptr(ptr) {}
    };

    // ---------- iterator ----------
    class iterator
    {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = T;
        using difference_type   = ptrdiff_t;
        using pointer           = T*;
        using reference         = T&;

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
        bool operator<=(const iterator& other) const noexcept { return m_ptr <= other.m_ptr; }
        bool operator> (const iterator& other) const noexcept { return m_ptr >  other.m_ptr; }
        bool operator>=(const iterator& other) const noexcept { return m_ptr >= other.m_ptr; }

        // 隐式转换为 const_iterator
        operator const_iterator() const noexcept { return const_iterator(m_ptr); }

    private:
        friend class Column<T>;

        T* m_ptr = nullptr;

        explicit iterator(T* ptr) noexcept : m_ptr(ptr) {}
    };

    // ---------- 构造 ----------
    Column() noexcept
        : m_type(ColumnType::Float64)
    {}

    explicit Column(ColumnType t) noexcept
        : m_type(t)
    {}

    ~Column()
    {
        delete[] m_data;
    }

    // ---- 拷贝 ----
    Column(const Column& other)
        : m_type(other.m_type)
        , m_size(other.m_size)
        , m_capacity(other.m_size)
    {
        if (m_capacity > 0)
        {
            m_data = new T[m_capacity];
            std::memcpy(m_data, other.m_data, m_size * sizeof(T));
        }
    }

    Column& operator=(const Column& other)
    {
        if (this != &other)
        {
            delete[] m_data;
            m_data = nullptr;
            m_type = other.m_type;
            m_size = other.m_size;
            m_capacity = other.m_size;
            if (m_capacity > 0)
            {
                m_data = new T[m_capacity];
                std::memcpy(m_data, other.m_data, m_size * sizeof(T));
            }
        }
        return *this;
    }

    // ---- 移动 ----
    Column(Column&& other) noexcept
        : m_data(other.m_data)
        , m_size(other.m_size)
        , m_capacity(other.m_capacity)
        , m_type(other.m_type)
    {
        other.m_data = nullptr;
        other.m_size = 0;
        other.m_capacity = 0;
    }

    Column& operator=(Column&& other) noexcept
    {
        if (this != &other)
        {
            delete[] m_data;
            m_data = other.m_data;
            m_size = other.m_size;
            m_capacity = other.m_capacity;
            m_type = other.m_type;
            other.m_data = nullptr;
            other.m_size = 0;
            other.m_capacity = 0;
        }
        return *this;
    }

    // -------- AbstractColumn 接口 --------
    ColumnType type()          const noexcept override { return m_type; }
    size_t     size()          const noexcept override { return m_size; }
    bool       empty()         const noexcept override { return m_size == 0; }

    void clear() override
    {
        delete[] m_data;
        m_data = nullptr;
        m_size = 0;
        m_capacity = 0;
        m_minMaxValid = false;
    }

    std::string typeName() const override
    {
        return "float64";
    }

    double getDouble(size_t idx) const override
    {
        return static_cast<double>(m_data[idx]);
    }

    void pushFromString(const std::string& s) override
    {
        char* end = nullptr;
        double val = std::strtod(s.c_str(), &end);

        if (end != s.c_str() && *end == '\0')
        {
            push_back(static_cast<T>(val));
        }
        else
        {
            push_back(std::numeric_limits<T>::quiet_NaN());
        }
    }

    std::pair<double, double> rangeMinMax(size_t begin, size_t end) const override
    {
        double vmin = std::numeric_limits<double>::max();
        double vmax = -std::numeric_limits<double>::max();

        for (size_t i = begin; i < end; ++i)
        {
            if (std::isnan(m_data[i]))
                continue;
            double v = static_cast<double>(m_data[i]);
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
        }

        return { vmin, vmax };
    }

    double cachedMin() const noexcept override { return m_cachedMin; }
    double cachedMax() const noexcept override { return m_cachedMax; }
    bool   hasCachedMinMax() const noexcept override { return m_minMaxValid; }

    void recalcMinMax() override
    {
        m_minMaxValid = false;
        if (m_size == 0)
            return;

        double vmin = std::numeric_limits<double>::max();
        double vmax = -std::numeric_limits<double>::max();
        bool hasValid = false;

        for (size_t i = 0; i < m_size; ++i)
        {
            if (std::isnan(m_data[i]))
                continue;
            double v = static_cast<double>(m_data[i]);
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

    // -------- Column 特有接口 --------
    T operator[](size_t idx) const noexcept
    {
        return m_data[idx];
    }

    T& operator[](size_t idx) noexcept
    {
        return m_data[idx];
    }

    // 类似 std::vector 的扩容语义
    void push_back(T val)
    {
        if (m_size >= m_capacity)
        {
            size_t newCap = (m_capacity == 0) ? 4096 : m_capacity * 2;
            T* newData = new T[newCap];
            if (m_data)
            {
                std::memcpy(newData, m_data, m_size * sizeof(T));
                delete[] m_data;
            }
            m_data = newData;
            m_capacity = newCap;
        }
        m_data[m_size++] = val;

        // 增量更新 min/max 缓存（O(1)）
        if (!std::isnan(val))
        {
            double v = static_cast<double>(val);
            if (!m_minMaxValid)
            {
                m_cachedMin = v;
                m_cachedMax = v;
                m_minMaxValid = true;
            }
            else
            {
                if (v < m_cachedMin) m_cachedMin = v;
                if (v > m_cachedMax) m_cachedMax = v;
            }
        }
    }

    const T& back() const noexcept
    {
        static T s_empty = {};
        if (m_size == 0)
            return s_empty;
        return m_data[m_size - 1];
    }

    T& back() noexcept
    {
        return m_data[m_size - 1];
    }

    // -------- 迭代器 --------
    const_iterator begin()  const noexcept { return const_iterator(m_data); }
    const_iterator end()    const noexcept { return const_iterator(m_data + m_size); }
    iterator       begin()        noexcept { return iterator(m_data); }
    iterator       end()          noexcept { return iterator(m_data + m_size); }

    // 批量追加
    template <typename InputIt>
    void append(InputIt first, InputIt last)
    {
        for (; first != last; ++first)
        {
            push_back(static_cast<T>(*first));
        }
    }

    void append(const std::vector<T>& vec)
    {
        append(vec.begin(), vec.end());
    }

    // 预留容量
    void reserve(size_t cap)
    {
        if (cap > m_capacity)
        {
            T* newData = new T[cap];
            if (m_data)
            {
                std::memcpy(newData, m_data, m_size * sizeof(T));
                delete[] m_data;
            }
            m_data = newData;
            m_capacity = cap;
        }
    }

    size_t capacity() const noexcept { return m_capacity; }

    // 裸指针访问（零开销内联遍历，用于绘图热路径）
    const T* data() const noexcept { return m_data; }
    T* data() noexcept { return m_data; }

private:
    T*      m_data     = nullptr;
    size_t  m_size     = 0;
    size_t  m_capacity = 0;
    ColumnType m_type;

    // min/max 缓存（全列，加载/表达式后自动计算）
    double  m_cachedMin = 0.0;
    double  m_cachedMax = 0.0;
    bool    m_minMaxValid = false;
};

} // namespace viewer