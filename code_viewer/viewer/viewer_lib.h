#pragma once

#include "code_viewer/datamgr/data_manager.h"

namespace viewer
{

class ViewerProcess
{
    ViewerProcess();
    ~ViewerProcess() = default;

    static ViewerProcess* __inst;

public:
    static ViewerProcess* instance() noexcept { return __inst; }

    // Load a CSV file into the data manager
    // Returns true on success, false on failure
    bool LoadCSV(const std::string& path);
    bool LoadCSV(const std::string& path, char delimiter, char quote = '"');

    DataManager&       GetDataManager()       noexcept { return m_data; }
    const DataManager& GetDataManager() const noexcept { return m_data; }

    const std::string& GetLastError() const noexcept { return m_lastError; }

private:
    // Auto-detect whether row 0 is a header
    bool detectHeader(const std::vector<std::string>& firstRow);

    // Sanitize a column name: trim, replace special chars, ensure uniqueness
    std::vector<std::string> sanitizeColumnNames(const std::vector<std::string>& rawNames);

    DataManager m_data;
    std::string m_lastError;
};

} // namespace viewer