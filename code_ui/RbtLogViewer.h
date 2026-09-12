#pragma once

#include <QAbstractScrollArea>
#include <QAbstractTableModel>
#include <QFont>
#include <QMainWindow>
#include <QStringList>
#include <QVector>

#include <memory>

#include "code_viewer/textmgr/rbt_text_manager.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableView;
class QTabWidget;

class RbtPatternTableModel final : public QAbstractTableModel
{
public:
    explicit RbtPatternTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    bool setData(const QModelIndex& index, const QVariant& value,
                 int role) override;
    bool removeRows(int row, int count, const QModelIndex& parent = {}) override;

    void setRules(QVector<viewer::RbtPatternRule> rules);
    const QVector<viewer::RbtPatternRule>& rules() const noexcept { return m_rules; }
    void addRule();

private:
    QVector<viewer::RbtPatternRule> m_rules;
};

class RbtLogTextView final : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit RbtLogTextView(QWidget* parent = nullptr);
    ~RbtLogTextView() override;

    void setDocument(std::shared_ptr<const viewer::RbtTextDocument> document);
    void setMatchIndex(std::shared_ptr<const viewer::RbtMatchIndex> matchIndex);
    void clearFile();
    qsizetype lineCount() const noexcept;
    quint64 fileSize() const noexcept;
    QString filePath() const;
    quint64 searchStartOffset(bool backward) const;
    void setSearchMatch(quint64 byteOffset, qsizetype byteLength);
    void jumpToLine(qsizetype zeroBasedLine, bool selectLine = true);
    qsizetype navigationAnchorLine() const noexcept;

Q_SIGNALS:
    void currentLineChanged(qsizetype oneBasedLine);
    void findRequested();
    void findNextRequested(bool backward);
    void markRequested(const QString& filePath, qsizetype zeroBasedLine, bool allPlots);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    struct TextPosition
    {
        qsizetype line = 0;
        qsizetype column = 0;
    };

    QString lineText(qsizetype line) const;
    quint64 lineStart(qsizetype line) const;
    TextPosition positionAt(const QPoint& point) const;
    void updateScrollBar();
    void copySelection() const;
    void selectAllText();
    void selectWordAt(const TextPosition& position);
    bool hasSelection() const;
    static bool positionLess(const TextPosition& lhs, const TextPosition& rhs);

    std::shared_ptr<const viewer::RbtTextDocument> m_document;
    std::shared_ptr<const viewer::RbtMatchIndex> m_matchIndex;
    QFont m_textFont;
    TextPosition m_anchor;
    TextPosition m_cursor;
    bool m_selecting = false;
    qint64 m_searchByteOffset = -1;
    qsizetype m_searchByteLength = 0;
    qsizetype m_currentPatternLine = -1;
};

class RbtLogViewerWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit RbtLogViewerWindow(QWidget* parent = nullptr);
    void openFiles(const QStringList& paths);
    bool openFileAtLine(const QString& path, qsizetype zeroBasedLine,
                        QString* error = nullptr);
    void releaseFiles();

Q_SIGNALS:
    void markRequested(const QString& filePath, qsizetype zeroBasedLine, bool allPlots);
    void jumpResult(const QString& filePath, qsizetype zeroBasedLine,
                    bool success, const QString& reason);

private:
    void presentWindow();
    void beginOpenFile(const QString& path);
    void beginFind(bool backward);
    void applyPatternChanges();
    void navigatePattern(viewer::RbtNavigateAction action);
    void updateNavigationControls();
    void setPatternDirty(bool dirty);
    void updateStatus(qsizetype oneBasedLine = 1);

    viewer::RbtTextManager* m_textManager = nullptr;
    QComboBox* m_files = nullptr;
    QLineEdit* m_findText = nullptr;
    QCheckBox* m_caseSensitive = nullptr;
    QPushButton* m_findPrevious = nullptr;
    QPushButton* m_findNext = nullptr;
    QLabel* m_status = nullptr;
    RbtLogTextView* m_textView = nullptr;
    QTabWidget* m_tabs = nullptr;
    QWidget* m_logTab = nullptr;
    QWidget* m_patternTab = nullptr;
    RbtPatternTableModel* m_patternModel = nullptr;
    QTableView* m_patternTable = nullptr;
    QPushButton* m_addPattern = nullptr;
    QPushButton* m_removePattern = nullptr;
    QPushButton* m_updatePatterns = nullptr;
    QLabel* m_patternStatus = nullptr;
    QComboBox* m_navigationPattern = nullptr;
    QPushButton* m_firstMatch = nullptr;
    QPushButton* m_previousMatch = nullptr;
    QPushButton* m_nextMatch = nullptr;
    QPushButton* m_lastMatch = nullptr;
    QLabel* m_matchPosition = nullptr;
    bool m_patternDirty = false;
    QString m_pendingJumpPath;
    qsizetype m_pendingJumpLine = -1;
};
