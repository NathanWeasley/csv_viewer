#include "wire.h"

#include <cstdio>
#include <cstring>
#include <stdexcept>

static uint64_t readVarint(std::string_view payload, size_t& offset)
{
    uint64_t result = 0;
    for (int shift = 0; shift < 64; shift += 7)
    {
        if (offset >= payload.size())
        {
            throw std::runtime_error("varint overrun");
        }
        const auto byte = static_cast<unsigned char>(payload[offset++]);
        if (shift == 63 && byte > 1)
        {
            throw std::runtime_error("varint too long");
        }
        result |= static_cast<uint64_t>(byte & 0x7F) << shift;
        if ((byte & 0x80) == 0)
        {
            return result;
        }
    }

    throw std::runtime_error("varint too long");
}

static void ensureAvailable(std::string_view payload, size_t offset, size_t length)
{
    if (offset > payload.size() || length > payload.size() - offset)
    {
        throw std::runtime_error("truncated field");
    }
}

static uint64_t readFixed64(const char* pointer)
{
    uint64_t bits = 0;
    for (int byteIndex = 0; byteIndex < 8; ++byteIndex)
    {
        bits |= static_cast<uint64_t>(static_cast<unsigned char>(pointer[byteIndex]))
                << (byteIndex * 8);
    }
    return bits;
}

static uint32_t readFixed32(const char* pointer)
{
    uint32_t bits = 0;
    for (int byteIndex = 0; byteIndex < 4; ++byteIndex)
    {
        bits |= static_cast<uint32_t>(static_cast<unsigned char>(pointer[byteIndex]))
                << (byteIndex * 8);
    }
    return bits;
}

static double decodeDouble(const char* pointer)
{
    const uint64_t bits = readFixed64(pointer);
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

static float decodeFloat(const char* pointer)
{
    const uint32_t bits = readFixed32(pointer);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

Fields decodeMessage(std::string_view payload)
{
    Fields fields;
    size_t offset = 0;
    while (offset < payload.size())
    {
        const uint64_t tag = readVarint(payload, offset);
        if ((tag >> 3) == 0 || (tag >> 3) > 0x1FFFFFFFu)
        {
            throw std::runtime_error("bad field number");
        }
        const uint32_t fieldNumber = static_cast<uint32_t>(tag >> 3);
        const uint32_t wireType = static_cast<uint32_t>(tag & 7);
        WireValue value;

        switch (wireType)
        {
        case 0:
            value.wireType = WT::Varint;
            value.unsignedValue = readVarint(payload, offset);
            break;

        case 1:
        {
            ensureAvailable(payload, offset, 8);
            value.wireType = WT::Fixed64;
            value.doubleValue = decodeDouble(payload.data() + offset);
            offset += 8;
            break;
        }

        case 5:
        {
            ensureAvailable(payload, offset, 4);
            value.wireType = WT::Fixed32;
            value.doubleValue = decodeFloat(payload.data() + offset);
            offset += 4;
            break;
        }

        case 2:
        {
            const uint64_t length = readVarint(payload, offset);
            if (length > static_cast<uint64_t>(payload.size()))
            {
                throw std::runtime_error("truncated field");
            }
            const size_t byteLength = static_cast<size_t>(length);
            ensureAvailable(payload, offset, byteLength);
            value.wireType = WT::Bytes;
            value.bytes = payload.substr(offset, byteLength);
            offset += byteLength;
            break;
        }

        default:
            throw std::runtime_error("bad wire type");
        }

        fields.emplace(fieldNumber, value);
    }

    return fields;
}

std::vector<double> unpackDoubles(std::string_view bytes)
{
    if (bytes.size() % 8 != 0)
    {
        throw std::runtime_error("packed double field has invalid length");
    }

    std::vector<double> values;
    values.reserve(bytes.size() / 8);
    for (size_t offset = 0; offset + 8 <= bytes.size(); offset += 8)
    {
        values.push_back(decodeDouble(bytes.data() + offset));
    }
    return values;
}

std::vector<int32_t> unpackSint32(std::string_view bytes)
{
    std::vector<int32_t> values;
    size_t offset = 0;
    while (offset < bytes.size())
    {
        values.push_back(decodeZigZag(readVarint(bytes, offset)));
    }
    return values;
}
