#pragma once

#include <string>

void exactDecimal(double value, std::string& digits, int& scale);

std::string formatGeneral(double value, int precision);

std::string printNumber(double value);

std::string jsonEscape(const std::string& value);
