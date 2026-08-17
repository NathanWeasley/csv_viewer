#include "num.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>

namespace
{

using Big = std::vector<uint32_t>;
constexpr uint32_t kBase = 1000000000u;

void multiplySmall(Big& value, uint32_t multiplier)
{
    uint64_t carry = 0;
    for (size_t i = 0; i < value.size(); ++i)
    {
        const uint64_t current = static_cast<uint64_t>(value[i]) * multiplier + carry;
        value[i] = static_cast<uint32_t>(current % kBase);
        carry = current / kBase;
    }

    while (carry)
    {
        value.push_back(static_cast<uint32_t>(carry % kBase));
        carry /= kBase;
    }
}

std::string toString(const Big& value)
{
    if (value.empty())
    {
        return "0";
    }

    std::string result = std::to_string(value.back());
    char buffer[16];
    for (size_t i = value.size() - 1; i-- > 0;)
    {
        std::snprintf(buffer, sizeof(buffer), "%09u", value[i]);
        result += buffer;
    }
    return result;
}

} // namespace

void exactDecimal(double value, std::string& digits, int& scale)
{
    value = std::fabs(value);
    if (value == 0.0)
    {
        digits = "0";
        scale = 0;
        return;
    }

    int binaryExponent = 0;
    const double fraction = std::frexp(value, &binaryExponent);
    uint64_t mantissa = static_cast<uint64_t>(std::ldexp(fraction, 53));
    int exponent = binaryExponent - 53;

    while (mantissa && (mantissa & 1) == 0)
    {
        mantissa >>= 1;
        ++exponent;
    }

    Big bigValue;
    uint64_t remaining = mantissa;
    while (remaining)
    {
        bigValue.push_back(static_cast<uint32_t>(remaining % kBase));
        remaining /= kBase;
    }

    if (exponent >= 0)
    {
        for (int i = 0; i < exponent; ++i)
        {
            multiplySmall(bigValue, 2);
        }
        scale = 0;
    }
    else
    {
        for (int i = 0; i < -exponent; ++i)
        {
            multiplySmall(bigValue, 5);
        }
        scale = -exponent;
    }

    digits = toString(bigValue);
}

std::string formatGeneral(double value, int precision)
{
    std::string digits;
    int scale = 0;
    exactDecimal(value, digits, scale);
    const bool negative = std::signbit(value);

    if (digits == "0")
    {
        return negative ? "-0" : "0";
    }

    int exponent = static_cast<int>(digits.size()) - 1 - scale;

    auto roundTo = [](std::string significand, int keep, int& exponentAdjustment)
    {
        exponentAdjustment = 0;
        if (keep >= static_cast<int>(significand.size()))
        {
            return significand;
        }
        if (keep < 0)
        {
            return std::string("0");
        }
        const bool roundUp = significand[keep] >= '5';
        significand.resize(keep);
        if (roundUp)
        {
            int i = keep - 1;
            for (; i >= 0; --i)
            {
                if (significand[i] == '9')
                {
                    significand[i] = '0';
                }
                else
                {
                    ++significand[i];
                    break;
                }
            }
            if (i < 0)
            {
                significand.insert(significand.begin(), '1');
                significand.pop_back();
                exponentAdjustment = 1;
            }
        }

        return significand;
    };

    int exponentAdjustment = 0;
    std::string significand = roundTo(digits, precision, exponentAdjustment);
    exponent += exponentAdjustment;

    const size_t lastNonzero = significand.find_last_not_of('0');
    significand = (lastNonzero == std::string::npos) ? "0" : significand.substr(0, lastNonzero + 1);
    if (significand == "0")
    {
        return negative ? "-0" : "0";
    }

    std::string output;
    if (exponent < -4 || exponent >= precision)
    {
        output += significand[0];
        if (significand.size() > 1)
        {
            output += ".";
            output += significand.substr(1);
        }
        char buffer[16];
        std::snprintf(buffer,
                      sizeof(buffer),
                      "e%c%02d",
                      exponent < 0 ? '-' : '+',
                      exponent < 0 ? -exponent : exponent);
        output += buffer;
    }
    else if (exponent >= 0)
    {
        if (static_cast<int>(significand.size()) > exponent + 1)
        {
            output = significand.substr(0, exponent + 1) + "." + significand.substr(exponent + 1);
        }
        else
        {
            output = significand;
            output.append(exponent + 1 - static_cast<int>(significand.size()), '0');
        }
    }
    else
    {
        output = "0.";
        output.append(-exponent - 1, '0');
        output += significand;
    }
    return negative ? "-" + output : output;
}

std::string printNumber(double value)
{
    if (std::isnan(value) || std::isinf(value))
    {
        return "null";
    }
    if (value == 0.0)
    {
        return std::signbit(value) ? "-0" : "0";
    }

    const double minInteger = static_cast<double>(std::numeric_limits<long long>::min());
    const double maxInteger = -minInteger;
    if (value >= minInteger && value < maxInteger &&
        value == static_cast<double>(static_cast<long long>(value)))
    {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
        return buffer;
    }

    std::string result = formatGeneral(value, 15);
    if (std::strtod(result.c_str(), nullptr) == value)
    {
        return result;
    }
    return formatGeneral(value, 17);
}

std::string jsonEscape(const std::string& value)
{
    std::string output = "\"";

    for (const unsigned char character : value)
    {
        switch (character)
        {
        case '"':
            output += "\\\"";
            break;
        case '\\':
            output += "\\\\";
            break;
        case '\b':
            output += "\\b";
            break;
        case '\f':
            output += "\\f";
            break;
        case '\n':
            output += "\\n";
            break;
        case '\r':
            output += "\\r";
            break;
        case '\t':
            output += "\\t";
            break;
        default:
            if (character < 0x20)
            {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", character);
                output += buffer;
            }
            else
            {
                output += static_cast<char>(character);
            }
        }
    }
    output += '"';
    return output;
}
