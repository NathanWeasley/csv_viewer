#pragma once

#include <QObject>
#include <QStringList>

#include "code_viewer/base/base_def.h"
#include "code_viewer/datamgr/data_manager.h"
#include "code_viewer/plotmgr/plot_manager.h"
#include "code_viewer/plotmgr/cursor/cursor_manager.h"
#include "code_viewer/stylemgr/style_manager.h"
#include "code_logparse/binary_log_types.h"
#include "code_logparse/binary_log_parser.h"
#include "code_logparse/ziplog/zip_entry_info.h"

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
    bool AdoptBinaryLog(logparse::ParseResult&& result, const std::string& sourcePath);

    static bool ReadZipCatalog(
        const std::filesystem::path& archivePath,
        std::vector<logparse::ziplog::ZipEntryInfo>& entries,
        std::string& error);
    static logparse::ParseResult ParseZipEntries(
        const std::filesystem::path& archivePath,
        const std::vector<uint64_t>& entryIndices,
        const logparse::ParseOptions& options = {});

    DataManager&       GetDataManager()       noexcept { return m_data; }
    const DataManager& GetDataManager() const noexcept { return m_data; }

    PlotManager&       GetPlotManager()       noexcept { return m_plots; }
    const PlotManager& GetPlotManager() const noexcept { return m_plots; }

    CursorManager&       GetCursorManager()       noexcept { return m_cursors; }
    const CursorManager& GetCursorManager() const noexcept { return m_cursors; }

    StyleManager&       GetStyleManager()       noexcept { return m_styles; }
    const StyleManager& GetStyleManager() const noexcept { return m_styles; }

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

    void LoadSkippedFiles(const QStringList& files);

public Q_SLOTS:
    /// Clear all loaded data.
    void Clear();

    /// Slot: receive a list of CSV file paths and load each one
    void OnLoadCSV(const QStringList& files);
    void OnLoadCSV(const QStringList& files, bool skipInvalidFiles);

private:
    // Auto-detect whether row 0 is a header
    bool detectHeader(const std::vector<std::string>& firstRow);

    // Sanitize a column name: trim, replace special chars, ensure uniqueness
    std::vector<std::string> sanitizeColumnNames(const std::vector<std::string>& rawNames);

    DataManager   m_data;
    PlotManager   m_plots;
    CursorManager m_cursors;
    StyleManager  m_styles;
    std::string   m_lastError;
};

} // namespace viewer
