#pragma once

#include <cstdint>

#if defined(VIEWER_LIB)
#define VIEWER_API __declspec(dllexport)
#else
#define VIEWER_API __declspec(dllimport)
#endif

namespace viewer
{

using VFLOAT = double;

}