#pragma once

#include <QtCore/qglobal.h>

#ifndef BUILD_STATIC
# if defined(LIBQCUSTOMPLOT_LIB)
#  define LIBQCUSTOMPLOT_EXPORT Q_DECL_EXPORT
# else
#  define LIBQCUSTOMPLOT_EXPORT Q_DECL_IMPORT
# endif
#else
# define LIBQCUSTOMPLOT_EXPORT
#endif
