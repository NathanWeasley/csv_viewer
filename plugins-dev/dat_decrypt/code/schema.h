#pragma once

#include <cstdint>
#include <string>
#include <vector>

enum class T
{
    Str,
    I32,
    U32,
    U64S,
    F64,
    F64Arr,
    I32Arr,
    Msg,
    Rep
};

struct Field;
using Schema = std::vector<Field>;

struct Field
{
    const char* key;
    uint32_t no;
    T type;
    int count = 0;
    const Schema* sub = nullptr;

    Field(const char* fieldKey, uint32_t fieldNumber, T fieldType)
        : key(fieldKey), no(fieldNumber), type(fieldType)
    {
    }

    Field(const char* fieldKey, uint32_t fieldNumber, T fieldType, int fieldCount)
        : key(fieldKey), no(fieldNumber), type(fieldType), count(fieldCount)
    {
    }

    Field(const char* fieldKey, uint32_t fieldNumber, T fieldType, const Schema* subSchema)
        : key(fieldKey), no(fieldNumber), type(fieldType), sub(subSchema)
    {
    }

    Field(const char* fieldKey,
          uint32_t fieldNumber,
          T fieldType,
          const Schema* subSchema,
          int fieldCount)
        : key(fieldKey), no(fieldNumber), type(fieldType), count(fieldCount), sub(subSchema)
    {
    }
};

const Schema* lookupTable(const std::string& name);
