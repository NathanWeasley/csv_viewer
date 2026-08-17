#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "dat_converter.h"

namespace datconv::detail
{

void materializeMessage(const Schema& schema,
                        std::string_view payload,
                        DatConverter::Value& destination,
                        std::size_t recordIndex,
                        const std::string& path,
                        const DatConverter::ParseOptions& options,
                        std::vector<DatConverter::Diagnostic>& diagnostics);

} // namespace datconv::detail
