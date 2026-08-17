#pragma once

#include <cstddef>
#include <cstdint>

class HikCrc32
{
    uint32_t table_[256];

public:
    HikCrc32()
    {
        for (int i = 0; i < 256; ++i)
        {
            uint32_t c = static_cast<uint32_t>(i);
            for (int k = 0; k < 8; ++k)
            {
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            }
            table_[i] = c;
        }
    }

    ~HikCrc32() = default;

    uint32_t calculate(const uint8_t* pointer, size_t length) const
    {
        uint32_t c = 0xFFFFFFFFu;

        for (size_t i = 0; i < length; ++i)
        {
            c = table_[(c ^ pointer[i]) & 0xFFu] ^ (c >> 8);
        }

        return c;
    }
};

inline uint32_t crc32(const uint8_t* pointer, size_t length)
{
    static const HikCrc32 crc;

    return crc.calculate(pointer, length);
}
