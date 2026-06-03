#pragma once

#include "code_viewer/datamgr/data_manager.h"

namespace viewer
{

class ViewerProcess
{
    ViewerProcess();
    ~ViewerProcess() = default;

    static ViewerProcess * __inst;

public:
    ViewerProcess * instance() noexcept { return __inst; }
    const ViewerProcess * instance() const noexcept { return __inst; }
};

}
