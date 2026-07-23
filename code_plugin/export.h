#pragma once

#if defined(LIBPLUGINMGR_LIB)
#define PM_API __declspec(dllexport)
#else
#define PM_API __declspec(dllimport)
#endif
