#pragma once

#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <vector>

inline bool readFile(const char* path, std::vector<uint8_t>& data)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input)
    {
        return false;
    }

    const std::ifstream::pos_type end = input.tellg();
    if (end < 0 || static_cast<uintmax_t>(end) > std::numeric_limits<size_t>::max() ||
        static_cast<uintmax_t>(end) >
            static_cast<uintmax_t>(std::numeric_limits<std::streamsize>::max()))
    {
        return false;
    }

    data.resize(static_cast<size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!data.empty())
    {
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }

    return static_cast<bool>(input);
}
