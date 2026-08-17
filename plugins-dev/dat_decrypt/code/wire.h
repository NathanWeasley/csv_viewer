#pragma once

#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

// Protobuf wire format.

enum class WT
{
    Varint,
    Fixed64,
    Bytes,
    Fixed32
};

struct WireValue
{
    WT wireType;
    uint64_t unsignedValue = 0;
    double doubleValue = 0;
    std::string_view bytes;
};

using Fields = std::multimap<uint32_t, WireValue>;

Fields decodeMessage(std::string_view payload);

inline int32_t decodeZigZag(uint64_t value)
{
    return static_cast<int32_t>((value >> 1) ^ (~(value & 1) + 1));
}

std::vector<double> unpackDoubles(std::string_view bytes);
std::vector<int32_t> unpackSint32(std::string_view bytes);
