#pragma once

#include <QObject>
#include <QStringList>

#include "code_viewer/datamgr/data_manager.h"

#if defined(VIEWER_LIB)
#define VIEWER_API __declspec(dllexport)
#else
#define VIEWER_API __declspec(dllimport)
#endif

namespace viewer
{

class VIEWER_API Viewer
    : public QObject
{
    Q_OBJECT

public:
    explicit Viewer(QObject* parent = nullptr);
    ~Viewer() override = default;

    // Load a CSV file into the data manager
    // Returns true on success, false on failure
    bool LoadCSV(const std::string& path);
    bool LoadCSV(const std::string& path, char delimiter, char quote = '"');

    DataManager&       GetDataManager()       noexcept { return m_data; }
    const DataManager& GetDataManager() const noexcept { return m_data; }

    const std::string& GetLastError() const noexcept { return m_lastError; }

Q_SIGNALS:
    /// Emitted when loading starts. totalFiles = number of CSVs to load.
    void LoadStarted(int totalFiles);

    /// Emitted per-file internal progress. globalProgress is 0.0 ~ 1.0 over all files.
    void BusyProgressChanged(float globalProgress);

    /// Emitted when all files are loaded (or no files selected).
    void LoadFinished();

    /// Emitted when column validation fails across files. UI should show error and clear.
    void LoadError(const QString& message);

public Q_SLOTS:
    /// Clear all loaded data.
    void Clear();

    /// Slot: receive a list of CSV file paths and load each one
    void OnLoadCSV(const QStringList& files);

private:
    // Auto-detect whether row 0 is a header
    bool detectHeader(const std::vector<std::string>& firstRow);

    // Sanitize a column name: trim, replace special chars, ensure uniqueness
    std::vector<std::string> sanitizeColumnNames(const std::vector<std::string>& rawNames);

    DataManager m_data;
    std::string m_lastError;
};

} // namespace viewer