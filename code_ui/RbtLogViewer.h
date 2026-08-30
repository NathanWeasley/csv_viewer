#pragma once

#include <QAbstractScrollArea>
#include <QFile>
#include <QMainWindow>
#include <QStringList>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;

class RbtLogTextView final : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit RbtLogTextView(QWidget* parent = nullptr);
    ~RbtLogTextView() override;

    bool setIndexedFile(const QString& path,
                        QVector<quint64> lineOffsets,
                        QString* error = nullptr);
    void clearFile();
    qsizetype lineCount() const noexcept { return m_lineOffsets.size(); }
    quint64 fileSize() const noexcept { return static_cast<quint64>(m_fileSize); }
    QString filePath() const { return m_file.fileName(); }
    quint64 searchStartOffset(bool backward) const;
    void setSearchMatch(quint64 byteOffset, qsizetype byteLength);
    void jumpToLine(qsizetype zeroBasedLine);

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

    QFile m_file;
    uchar* m_mapped = nullptr;
    qint64 m_fileSize = 0;
    QVector<quint64> m_lineOffsets;
    QFont m_textFont;
    TextPosition m_anchor;
    TextPosition m_cursor;
    bool m_selecting = false;
    qint64 m_searchByteOffset = -1;
    qsizetype m_searchByteLength = 0;
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
    void beginOpenFile(const QString& path);
    void beginFind(bool backward);
    void updateStatus(qsizetype oneBasedLine = 1);

    QComboBox* m_files = nullptr;
    QLineEdit* m_findText = nullptr;
    QCheckBox* m_caseSensitive = nullptr;
    QPushButton* m_findPrevious = nullptr;
    QPushButton* m_findNext = nullptr;
    QLabel* m_status = nullptr;
    RbtLogTextView* m_textView = nullptr;
    quint64 m_openGeneration = 0;
    quint64 m_findGeneration = 0;
    QString m_pendingJumpPath;
    qsizetype m_pendingJumpLine = -1;
};
