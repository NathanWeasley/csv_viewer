#pragma once

#include "code_viewer/base/base_def.h"
#include <deque>
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
    Unknown = 0,
    Int64,      // 所有值均为合法 int64
    Float64,    // 有非 int64 但可转为 double 的值，或含不可解析的字符串（对应位置存 NaN）
};

// ============================================================
// CellType: 单元格分类（用于类型推断阶段）
// ============================================================
enum class CellType : uint8_t
{
    Int = 0,
    Float,
    String
};

// ============================================================
// DataChunk: 固定大小数据块
// ============================================================
template <typename T>
struct DataChunk
{
    static constexpr size_t CHUNK_CAPACITY = __chunk_size;

    size_t  _size;
    T       _data[CHUNK_CAPACITY];

    // Chunk 元数据：本块内数据的 min/max（降采样加速用）
    T _min = std::numeric_limits<T>::max();
    T _max = -std::numeric_limits<T>::max();

    DataChunk()
        : _size(0)
    {}

    bool full() const noexcept { return _size >= CHUNK_CAPACITY; }
    size_t capacity() const noexcept { return CHUNK_CAPACITY; }

    // 更新元数据（遍历块内有效元素）
    void rebuildMeta()
    {
        _min = std::numeric_limits<T>::max();
        _max = -std::numeric_limits<T>::max();
        for (size_t i = 0; i < _size; ++i)
        {
            if constexpr (std::is_same<T, double>::value || std::is_same<T, float>::value)
            {
                if (std::isnan(_data[i])) continue;
            }
            if (_data[i] < _min) _min = _data[i];
            if (_data[i] > _max) _max = _data[i];
        }
    }
};

// 前向声明
template <typename T> class Column;

// ============================================================
// AbstractColumn: 多态基类
// ============================================================
struct AbstractColumn
{
    virtual ~AbstractColumn() = default;

    virtual ColumnType      type() const noexcept = 0;
    virtual size_t          size() const noexcept = 0;
    virtual bool            empty() const noexcept = 0;
    virtual void            pushFromString(const std::string& s) = 0;
    virtual double          getDouble(size_t idx) const = 0;
    virtual int64_t         getInt64(size_t idx) const = 0;
    virtual std::string     typeName() const = 0;
    virtual void            clear() = 0;

    // 克隆一个空列（同类型但无数据）
    virtual std::unique_ptr<AbstractColumn> cloneEmpty() const = 0;

    // 将自身所有数据以 double 形式拷贝到目标列（用于类型升级）
    virtual void copyToDoubleColumn(Column<double>* dst) const = 0;

    // Chunk 元数据访问（降采样加速用）
    virtual size_t              chunkCount() const = 0;
    virtual std::pair<double, double> chunkMinMax(size_t chunkIdx) const = 0;
    virtual void                rebuildAllChunkMeta() = 0;
    virtual size_t              chunkElementCount(size_t chunkIdx) const = 0;
    virtual double              chunkFirstElement(size_t chunkIdx) const = 0;
    virtual double              chunkLastElement(size_t chunkIdx) const = 0;
};

// ============================================================
// 类型辅助函数（inline，必须在 Column 特化之前定义）
// ============================================================

// 分类一个单元格字符串属于什么类型
inline CellType classifyCell(const std::string& s)
{
    if (s.empty())
        return CellType::Float;

    char* end = nullptr;
    std::strtoll(s.c_str(), &end, 10);
    if (end != s.c_str() && *end == '\0')
        return CellType::Int;

    end = nullptr;
    std::strtod(s.c_str(), &end);
    if (end != s.c_str() && *end == '\0')
        return CellType::Float;

    return CellType::String;
}

// ============================================================
// Column<T>: 分块存储列模板
// ============================================================
template <typename T>
class Column
    : public AbstractColumn
{
    static_assert(std::is_same<T, double>::value || std::is_same<T, int64_t>::value,
                  "Column<T> only supports double or int64_t");

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

        reference operator*()  const noexcept { return m_chunks->at(m_chunkIdx)._data[m_elemIdx]; }
        pointer   operator->() const noexcept { return &m_chunks->at(m_chunkIdx)._data[m_elemIdx]; }

        const_iterator& operator++()    noexcept { advanceLocal(1); return *this; }
        const_iterator  operator++(int) noexcept { auto tmp = *this; ++(*this); return tmp; }
        const_iterator& operator--()    noexcept { retreatLocal(1); return *this; }
        const_iterator  operator--(int) noexcept { auto tmp = *this; --(*this); return tmp; }

        const_iterator& operator+=(difference_type n) noexcept { advanceLocal(n); return *this; }
        const_iterator& operator-=(difference_type n) noexcept { retreatLocal(n); return *this; }

        reference operator[](difference_type n) const noexcept
        {
            size_t idx = m_globalIdx + n;
            return m_chunks->at(idx / __chunk_size)._data[idx % __chunk_size];
        }

        difference_type operator-(const const_iterator& other) const noexcept
        {
            return static_cast<difference_type>(m_globalIdx) - static_cast<difference_type>(other.m_globalIdx);
        }

        bool operator==(const const_iterator& other) const noexcept { return m_globalIdx == other.m_globalIdx; }
        bool operator!=(const const_iterator& other) const noexcept { return m_globalIdx != other.m_globalIdx; }
        bool operator< (const const_iterator& other) const noexcept { return m_globalIdx <  other.m_globalIdx; }
        bool operator<=(const const_iterator& other) const noexcept { return m_globalIdx <= other.m_globalIdx; }
        bool operator> (const const_iterator& other) const noexcept { return m_globalIdx >  other.m_globalIdx; }
        bool operator>=(const const_iterator& other) const noexcept { return m_globalIdx >= other.m_globalIdx; }

        size_t globalIndex() const noexcept { return m_globalIdx; }

    private:
        friend class Column<T>;

        const std::deque<DataChunk<T>>* m_chunks = nullptr;
        size_t m_globalIdx = 0;
        size_t m_chunkIdx  = 0;
        size_t m_elemIdx   = 0;

        const_iterator(const std::deque<DataChunk<T>>* chunks, size_t globalIdx)
            : m_chunks(chunks), m_globalIdx(globalIdx)
        {
            m_chunkIdx = globalIdx / __chunk_size;
            m_elemIdx  = globalIdx % __chunk_size;
        }

        inline void advanceLocal(difference_type n) noexcept
        {
            if (n == 0)
            {
                return;
            }

            m_globalIdx += n;
            m_chunkIdx = m_globalIdx / __chunk_size;
            m_elemIdx  = m_globalIdx % __chunk_size;
        }

        inline void retreatLocal(difference_type n) noexcept
        {
            advanceLocal(-n);
        }
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

        reference operator*()  const noexcept { return m_chunks->at(m_chunkIdx)._data[m_elemIdx]; }
        pointer   operator->() const noexcept { return &m_chunks->at(m_chunkIdx)._data[m_elemIdx]; }

        iterator& operator++()    noexcept { advanceLocal(1); return *this; }
        iterator  operator++(int) noexcept { auto tmp = *this; ++(*this); return tmp; }
        iterator& operator--()    noexcept { retreatLocal(1); return *this; }
        iterator  operator--(int) noexcept { auto tmp = *this; --(*this); return tmp; }

        iterator& operator+=(difference_type n) noexcept { advanceLocal(n); return *this; }
        iterator& operator-=(difference_type n) noexcept { retreatLocal(n); return *this; }

        reference operator[](difference_type n) const noexcept
        {
            size_t idx = m_globalIdx + n;
            return m_chunks->at(idx / __chunk_size)._data[idx % __chunk_size];
        }

        difference_type operator-(const iterator& other) const noexcept
        {
            return static_cast<difference_type>(m_globalIdx) - static_cast<difference_type>(other.m_globalIdx);
        }

        bool operator==(const iterator& other) const noexcept { return m_globalIdx == other.m_globalIdx; }
        bool operator!=(const iterator& other) const noexcept { return m_globalIdx != other.m_globalIdx; }
        bool operator< (const iterator& other) const noexcept { return m_globalIdx <  other.m_globalIdx; }
        bool operator<=(const iterator& other) const noexcept { return m_globalIdx <= other.m_globalIdx; }
        bool operator> (const iterator& other) const noexcept { return m_globalIdx >  other.m_globalIdx; }
        bool operator>=(const iterator& other) const noexcept { return m_globalIdx >= other.m_globalIdx; }

        // 隐式转换为 const_iterator
        operator const_iterator() const noexcept
        {
            const_iterator ci;
            ci.m_chunks = m_chunks;
            ci.m_globalIdx = m_globalIdx;
            ci.m_chunkIdx = m_chunkIdx;
            ci.m_elemIdx = m_elemIdx;
            return ci;
        }

        size_t globalIndex() const noexcept { return m_globalIdx; }

    private:
        friend class Column<T>;

        std::deque<DataChunk<T>>* m_chunks = nullptr;
        size_t m_globalIdx = 0;
        size_t m_chunkIdx  = 0;
        size_t m_elemIdx   = 0;

        iterator(std::deque<DataChunk<T>>* chunks, size_t globalIdx)
            : m_chunks(chunks), m_globalIdx(globalIdx)
        {
            m_chunkIdx = globalIdx / __chunk_size;
            m_elemIdx  = globalIdx % __chunk_size;
        }

        void advanceLocal(difference_type n) noexcept
        {
            if (n == 0)
            {
                return;
            }

            m_globalIdx += n;
            m_chunkIdx = m_globalIdx / __chunk_size;
            m_elemIdx  = m_globalIdx % __chunk_size;
        }

        void retreatLocal(difference_type n) noexcept
        {
            advanceLocal(-n);
        }
    };

    // ---------- 构造 ----------
    Column() noexcept
        : m_type(std::is_same<T, int64_t>::value ? ColumnType::Int64 : ColumnType::Float64)
    {}

    explicit Column(ColumnType t) noexcept
        : m_type(t)
    {}

    // -------- AbstractColumn 接口 --------
    ColumnType type()          const noexcept override { return m_type; }
    size_t     size()          const noexcept override { return m_size; }
    bool       empty()         const noexcept override { return m_size == 0; }
    void       clear()               override { m_chunks.clear(); m_size = 0; }

    std::string typeName() const override
    {
        return m_type == ColumnType::Int64 ? "int64" : "float64";
    }

    std::unique_ptr<AbstractColumn> cloneEmpty() const override
    {
        return std::make_unique<Column<T>>(m_type);
    }

    void copyToDoubleColumn(Column<double>* dst) const override
    {
        if constexpr (std::is_same_v<T, double>)
        {
            // 同类型：逐 chunk memcpy（追加语义，处理 dst 尾部未满情况）
            for (size_t i = 0; i < m_chunks.size(); ++i)
            {
                const auto& srcChunk = m_chunks[i];
                size_t remaining = srcChunk._size;
                size_t srcOffset = 0;

                while (remaining > 0)
                {
                    if (dst->m_chunks.empty() || dst->m_chunks.back().full())
                        dst->m_chunks.emplace_back();

                    auto& dstChunk = dst->m_chunks.back();
                    size_t copyCount = std::min(remaining,
                        DataChunk<double>::CHUNK_CAPACITY - dstChunk._size);

                    std::memcpy(dstChunk._data + dstChunk._size,
                               srcChunk._data + srcOffset,
                               copyCount * sizeof(double));

                    dstChunk._size += copyCount;
                    dst->m_size += copyCount;
                    remaining -= copyCount;
                    srcOffset += copyCount;
                }
            }
        }
        else
        {
            // Int64 → double：逐元素转换
            for (size_t i = 0; i < m_size; ++i)
                dst->push_back(static_cast<double>((*this)[i]));
        }
    }

    double getDouble(size_t idx) const override
    {
        return static_cast<double>((*this)[idx]);
    }

    int64_t getInt64(size_t idx) const override
    {
        if constexpr (std::is_same<T, int64_t>::value)
            return (*this)[idx];
        else
            return static_cast<int64_t>((*this)[idx]);
    }

    void pushFromString(const std::string& s) override
    {
        T val = parseValue(s);
        push_back(val);
    }

    // ----- Chunk 元数据访问 -----
    size_t chunkCount() const override { return m_chunks.size(); }

    std::pair<double, double> chunkMinMax(size_t chunkIdx) const override
    {
        const auto& c = m_chunks[chunkIdx];
        return { static_cast<double>(c._min), static_cast<double>(c._max) };
    }

    void rebuildAllChunkMeta() override
    {
        for (auto& c : m_chunks)
            c.rebuildMeta();
    }

    size_t chunkElementCount(size_t chunkIdx) const override
    {
        return m_chunks[chunkIdx]._size;
    }

    double chunkFirstElement(size_t chunkIdx) const override
    {
        return static_cast<double>(m_chunks[chunkIdx]._data[0]);
    }

    double chunkLastElement(size_t chunkIdx) const override
    {
        return static_cast<double>(m_chunks[chunkIdx]._data[m_chunks[chunkIdx]._size - 1]);
    }

    // -------- Column 特有接口 --------
    T operator[](size_t idx) const noexcept
    {
        return m_chunks[idx / __chunk_size]._data[idx % __chunk_size];
    }

    T& operator[](size_t idx) noexcept
    {
        return m_chunks[idx / __chunk_size]._data[idx % __chunk_size];
    }

    void push_back(T val)
    {
        if (m_chunks.empty() || m_chunks.back().full())
        {
            m_chunks.emplace_back();
        }
        auto& back = m_chunks.back();
        back._data[back._size++] = val;
        ++m_size;
    }

    const T& back() const noexcept
    {
        static T s_empty = {};
        if (m_chunks.empty())
        {
            return s_empty;
        }
        const auto& lastChunk = m_chunks.back();
        return lastChunk._data[lastChunk._size - 1];
    }

    T& back() noexcept
    {
        auto& lastChunk = m_chunks.back();
        return lastChunk._data[lastChunk._size - 1];
    }

    // -------- 迭代器 --------
    const_iterator begin()  const noexcept { return const_iterator(&m_chunks, 0); }
    const_iterator end()    const noexcept { return const_iterator(&m_chunks, m_size); }
    iterator       begin()        noexcept { return iterator(&m_chunks, 0); }
    iterator       end()          noexcept { return iterator(&m_chunks, m_size); }

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

private:
    static T parseValue(const std::string& s)
    {
        if constexpr (std::is_same<T, int64_t>::value)
        {
            char* end = nullptr;
            long long val = std::strtoll(s.c_str(), &end, 10);
            if (end != s.c_str() && *end == '\0')
                return static_cast<int64_t>(val);
            return 0;
        }
        else
        {
            char* end = nullptr;
            double val = std::strtod(s.c_str(), &end);
            if (end != s.c_str() && *end == '\0')
                return val;
            return std::numeric_limits<double>::quiet_NaN();
        }
    }

private:
    std::deque<DataChunk<T>> m_chunks;
    size_t m_size = 0;
    ColumnType m_type;
};

// ============================================================
// Column<double> 特化: pushFromString 处理 NaN
// ============================================================
template <>
inline void Column<double>::pushFromString(const std::string& s)
{
    char* end = nullptr;
    double val = std::strtod(s.c_str(), &end);

    if (end != s.c_str() && *end == '\0')
    {
        push_back(val);
    }
    else
    {
        push_back(std::numeric_limits<double>::quiet_NaN());
    }
}

// ============================================================
// Column<int64_t> 特化: pushFromString 严格匹配
// ============================================================
template <>
inline void Column<int64_t>::pushFromString(const std::string& s)
{
    char* end = nullptr;
    long long val = std::strtoll(s.c_str(), &end, 10);

    if (end != s.c_str() && *end == '\0')
    {
        push_back(static_cast<int64_t>(val));
    }
    else
    {
        push_back(0);  // 类型不匹配时由 DataManager 处理升级
    }
}

} // namespace viewer