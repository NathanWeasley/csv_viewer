#include "RbtLogViewer.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QTextLayout>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <limits>

namespace
{

constexpr qint64 kIndexChunkSize = 8LL * 1024LL * 1024LL;
constexpr qsizetype kMaximumIndexedLines = 25'000'000;

struct LineIndexResult
{
    QVector<quint64> offsets;
    qint64 fileSize = 0;
    QString error;
};

struct FindResult
{
    qint64 offset = -1;
    bool wrapped = false;
    QString error;
};

QFont logFont()
{
    const QStringList families = QFontDatabase::families();
    const QString family = families.contains(QStringLiteral("Consolas"), Qt::CaseInsensitive)
        ? QStringLiteral("Consolas") : QStringLiteral("Courier New");
    QFont font(family, 10);
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    return font;
}

QString byteCountText(quint64 bytes)
{
    static const char* units[] = {"B", "KiB", "MiB", "GiB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 3)
    {
        value /= 1024.0;
        ++unit;
    }
    return unit == 0
        ? QString::number(bytes) + QStringLiteral(" B")
        : QString::number(value, 'f', 1) + QLatin1Char(' ')
            + QString::fromLatin1(units[unit]);
}

LineIndexResult buildLineIndex(const QString& path)
{
    LineIndexResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.error = file.errorString();
        return result;
    }
    result.fileSize = file.size();
    result.offsets.reserve(static_cast<qsizetype>(
        std::min<qint64>(result.fileSize / 48 + 1, 4'000'000)));
    result.offsets.push_back(0);

    quint64 absoluteOffset = 0;
    while (!file.atEnd())
    {
        const QByteArray chunk = file.read(kIndexChunkSize);
        if (chunk.isEmpty() && file.error() != QFile::NoError)
        {
            result.error = file.errorString();
            result.offsets.clear();
            return result;
        }
        const char* data = chunk.constData();
        for (qsizetype index = 0; index < chunk.size(); ++index)
        {
            if (data[index] != '\n')
                continue;
            if (result.offsets.size() >= kMaximumIndexedLines)
            {
                result.error = QString::fromUtf8(u8"日志行数超过查看器上限（2500 万行）。");
                result.offsets.clear();
                return result;
            }
            result.offsets.push_back(absoluteOffset + static_cast<quint64>(index) + 1);
        }
        absoluteOffset += static_cast<quint64>(chunk.size());
    }
    return result;
}

QByteArray comparableBytes(QByteArray bytes, bool caseSensitive)
{
    return caseSensitive ? bytes : bytes.toLower();
}

qint64 findForwardRange(QFile& file,
                        const QByteArray& needle,
                        qint64 begin,
                        qint64 end,
                        bool caseSensitive)
{
    if (begin >= end)
        return -1;
    constexpr qint64 kChunk = 8LL * 1024LL * 1024LL;
    QByteArray tail;
    qint64 position = begin;
    while (position < end)
    {
        if (!file.seek(position))
            return -1;
        const QByteArray current = file.read(std::min(kChunk, end - position));
        if (current.isEmpty())
            break;
        const QByteArray combined = tail + current;
        const qint64 base = position - tail.size();
        const QByteArray comparable = comparableBytes(combined, caseSensitive);
        qsizetype found = comparable.indexOf(needle);
        while (found >= 0)
        {
            const qint64 absolute = base + found;
            if (absolute >= begin && absolute + needle.size() <= end)
                return absolute;
            found = comparable.indexOf(needle, found + 1);
        }
        const qsizetype overlap = std::min<qsizetype>(
            std::max<qsizetype>(0, needle.size() - 1), combined.size());
        tail = combined.right(overlap);
        position += current.size();
    }
    return -1;
}

qint64 findLastInRange(QFile& file,
                       const QByteArray& needle,
                       qint64 begin,
                       qint64 end,
                       bool caseSensitive)
{
    if (begin >= end)
        return -1;
    constexpr qint64 kChunk = 8LL * 1024LL * 1024LL;
    QByteArray tail;
    qint64 position = begin;
    qint64 last = -1;
    while (position < end)
    {
        if (!file.seek(position))
            return -1;
        const QByteArray current = file.read(std::min(kChunk, end - position));
        if (current.isEmpty())
            break;
        const QByteArray combined = tail + current;
        const qint64 base = position - tail.size();
        const QByteArray comparable = comparableBytes(combined, caseSensitive);
        qsizetype found = comparable.indexOf(needle);
        while (found >= 0)
        {
            const qint64 absolute = base + found;
            if (absolute >= begin && absolute + needle.size() <= end)
                last = absolute;
            found = comparable.indexOf(needle, found + 1);
        }
        const qsizetype overlap = std::min<qsizetype>(
            std::max<qsizetype>(0, needle.size() - 1), combined.size());
        tail = combined.right(overlap);
        position += current.size();
    }
    return last;
}

FindResult findInFile(const QString& path,
                      QByteArray needle,
                      qint64 start,
                      bool backward,
                      bool caseSensitive)
{
    FindResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
    {
        result.error = file.errorString();
        return result;
    }
    if (needle.isEmpty())
        return result;
    if (!caseSensitive)
        needle = needle.toLower();
    const qint64 size = file.size();
    start = std::clamp<qint64>(start, 0, size);

    if (backward)
    {
        result.offset = findLastInRange(file, needle, 0, start, caseSensitive);
        if (result.offset < 0)
        {
            result.offset = findLastInRange(file, needle, start, size, caseSensitive);
            result.wrapped = result.offset >= 0;
        }
    }
    else
    {
        result.offset = findForwardRange(file, needle, start, size, caseSensitive);
        if (result.offset < 0)
        {
            result.offset = findForwardRange(file, needle, 0, start, caseSensitive);
            result.wrapped = result.offset >= 0;
        }
    }
    return result;
}

} // namespace

RbtLogTextView::RbtLogTextView(QWidget* parent)
    : QAbstractScrollArea(parent), m_textFont(logFont())
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setFont(m_textFont);
    horizontalScrollBar()->hide();
    verticalScrollBar()->setSingleStep(3);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
        [this](int value)
        {
            viewport()->update();
            emit currentLineChanged(static_cast<qsizetype>(value) + 1);
        });
}

RbtLogTextView::~RbtLogTextView()
{
    clearFile();
}

bool RbtLogTextView::setIndexedFile(
    const QString& path, QVector<quint64> lineOffsets, QString* error)
{
    clearFile();
    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly))
    {
        if (error)
            *error = m_file.errorString();
        return false;
    }
    m_fileSize = m_file.size();
    if (m_fileSize > 0)
    {
        m_mapped = m_file.map(0, m_fileSize);
        if (!m_mapped)
        {
            if (error)
                *error = m_file.errorString();
            m_file.close();
            m_fileSize = 0;
            return false;
        }
    }
    m_lineOffsets = std::move(lineOffsets);
    if (m_lineOffsets.isEmpty())
        m_lineOffsets.push_back(0);
    m_anchor = {};
    m_cursor = {};
    m_searchByteOffset = -1;
    m_searchByteLength = 0;
    updateScrollBar();
    viewport()->update();
    return true;
}

void RbtLogTextView::clearFile()
{
    if (m_mapped)
        m_file.unmap(m_mapped);
    m_mapped = nullptr;
    m_file.close();
    m_file.setFileName(QString());
    m_fileSize = 0;
    m_lineOffsets.clear();
    m_searchByteOffset = -1;
    m_searchByteLength = 0;
    verticalScrollBar()->setRange(0, 0);
    viewport()->update();
}

QString RbtLogTextView::lineText(qsizetype line) const
{
    if (line < 0 || line >= m_lineOffsets.size() || !m_mapped)
        return {};
    const quint64 begin = m_lineOffsets[line];
    quint64 end = line + 1 < m_lineOffsets.size()
        ? m_lineOffsets[line + 1] : static_cast<quint64>(m_fileSize);
    if (end > begin && m_mapped[end - 1] == '\n')
        --end;
    return QString::fromUtf8(
        reinterpret_cast<const char*>(m_mapped + begin),
        static_cast<qsizetype>(end - begin));
}

quint64 RbtLogTextView::lineStart(qsizetype line) const
{
    if (m_lineOffsets.isEmpty())
        return 0;
    return m_lineOffsets[std::clamp<qsizetype>(line, 0, m_lineOffsets.size() - 1)];
}

void RbtLogTextView::updateScrollBar()
{
    const int maximum = static_cast<int>(std::min<qsizetype>(
        std::numeric_limits<int>::max(), std::max<qsizetype>(0, lineCount() - 1)));
    const int visibleLines = std::max(1, viewport()->height()
        / std::max(1, QFontMetrics(m_textFont).height()));
    verticalScrollBar()->setRange(0, maximum);
    verticalScrollBar()->setPageStep(visibleLines);
}

bool RbtLogTextView::positionLess(
    const TextPosition& lhs, const TextPosition& rhs)
{
    return lhs.line < rhs.line
        || (lhs.line == rhs.line && lhs.column < rhs.column);
}

bool RbtLogTextView::hasSelection() const
{
    return m_anchor.line != m_cursor.line || m_anchor.column != m_cursor.column;
}

void RbtLogTextView::paintEvent(QPaintEvent*)
{
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), palette().base());
    if (m_lineOffsets.isEmpty())
        return;

    const QFontMetrics metrics(m_textFont);
    const int digits = QString::number(m_lineOffsets.size()).size();
    const int marginWidth = metrics.horizontalAdvance(QLatin1Char('9')) * digits + 18;
    painter.fillRect(QRect(0, 0, marginWidth, viewport()->height()),
                     palette().alternateBase());
    painter.setPen(palette().mid().color());
    painter.drawLine(marginWidth - 1, 0, marginWidth - 1, viewport()->height());
    painter.setFont(m_textFont);

    TextPosition selectionBegin = m_anchor;
    TextPosition selectionEnd = m_cursor;
    if (positionLess(selectionEnd, selectionBegin))
        std::swap(selectionBegin, selectionEnd);

    qreal y = 0.0;
    const qreal textWidth = std::max(20, viewport()->width() - marginWidth - 10);
    for (qsizetype lineIndex = verticalScrollBar()->value();
         lineIndex < m_lineOffsets.size() && y < viewport()->height(); ++lineIndex)
    {
        const QString text = lineText(lineIndex);
        QTextLayout layout(text, m_textFont);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        qreal height = 0.0;
        while (true)
        {
            QTextLine line = layout.createLine();
            if (!line.isValid())
                break;
            line.setLineWidth(textWidth);
            line.setPosition(QPointF(0, height));
            height += line.height();
        }
        layout.endLayout();
        height = std::max<qreal>(height, metrics.height());

        QList<QTextLayout::FormatRange> formats;
        if (hasSelection() && lineIndex >= selectionBegin.line
            && lineIndex <= selectionEnd.line)
        {
            const qsizetype start = lineIndex == selectionBegin.line
                ? selectionBegin.column : 0;
            const qsizetype end = lineIndex == selectionEnd.line
                ? selectionEnd.column : text.size();
            if (end > start)
            {
                QTextLayout::FormatRange range;
                range.start = static_cast<int>(start);
                range.length = static_cast<int>(end - start);
                range.format.setBackground(palette().highlight());
                range.format.setForeground(palette().highlightedText());
                formats.push_back(range);
            }
        }

        painter.setPen(palette().text().color());
        layout.draw(&painter, QPointF(marginWidth + 6, y), formats);
        painter.setPen(palette().placeholderText().color());
        painter.drawText(QRectF(3, y, marginWidth - 10, metrics.height()),
                         Qt::AlignRight | Qt::AlignVCenter,
                         QString::number(lineIndex + 1));
        y += height;
    }
}

RbtLogTextView::TextPosition RbtLogTextView::positionAt(const QPoint& point) const
{
    TextPosition result;
    if (m_lineOffsets.isEmpty())
        return result;
    const QFontMetrics metrics(m_textFont);
    const int digits = QString::number(m_lineOffsets.size()).size();
    const int marginWidth = metrics.horizontalAdvance(QLatin1Char('9')) * digits + 18;
    const qreal textWidth = std::max(20, viewport()->width() - marginWidth - 10);
    qreal y = 0.0;
    result.line = verticalScrollBar()->value();
    for (qsizetype lineIndex = result.line; lineIndex < m_lineOffsets.size(); ++lineIndex)
    {
        const QString text = lineText(lineIndex);
        QTextLayout layout(text, m_textFont);
        QTextOption option;
        option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(option);
        layout.beginLayout();
        qreal height = 0.0;
        QVector<QTextLine> visualLines;
        while (true)
        {
            QTextLine visual = layout.createLine();
            if (!visual.isValid())
                break;
            visual.setLineWidth(textWidth);
            visual.setPosition(QPointF(0, height));
            height += visual.height();
            visualLines.push_back(visual);
        }
        layout.endLayout();
        height = std::max<qreal>(height, metrics.height());
        if (point.y() < y + height || lineIndex + 1 == m_lineOffsets.size())
        {
            result.line = lineIndex;
            result.column = text.size();
            const qreal localY = point.y() - y;
            for (const QTextLine& visual : visualLines)
            {
                if (localY < visual.y() + visual.height())
                {
                    result.column = visual.xToCursor(
                        point.x() - marginWidth - 6);
                    break;
                }
            }
            return result;
        }
        y += height;
        if (y >= viewport()->height())
            break;
    }
    result.line = m_lineOffsets.size() - 1;
    result.column = lineText(result.line).size();
    return result;
}

void RbtLogTextView::resizeEvent(QResizeEvent* event)
{
    QAbstractScrollArea::resizeEvent(event);
    updateScrollBar();
    viewport()->update();
}

void RbtLogTextView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !m_lineOffsets.isEmpty())
    {
        setFocus();
        m_anchor = m_cursor = positionAt(event->position().toPoint());
        m_selecting = true;
        emit currentLineChanged(m_cursor.line + 1);
        viewport()->update();
        event->accept();
        return;
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void RbtLogTextView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_selecting)
    {
        if (event->position().y() < 0)
            verticalScrollBar()->setValue(verticalScrollBar()->value() - 1);
        else if (event->position().y() >= viewport()->height())
            verticalScrollBar()->setValue(verticalScrollBar()->value() + 1);
        m_cursor = positionAt(event->position().toPoint());
        emit currentLineChanged(m_cursor.line + 1);
        viewport()->update();
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void RbtLogTextView::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_selecting)
    {
        m_selecting = false;
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseReleaseEvent(event);
}

void RbtLogTextView::selectWordAt(const TextPosition& position)
{
    const QString text = lineText(position.line);
    qsizetype begin = std::clamp<qsizetype>(position.column, 0, text.size());
    qsizetype end = begin;
    const auto isWord = [](QChar character)
    {
        return character.isLetterOrNumber() || character == QLatin1Char('_');
    };
    while (begin > 0 && isWord(text[begin - 1]))
        --begin;
    while (end < text.size() && isWord(text[end]))
        ++end;
    m_anchor = {position.line, begin};
    m_cursor = {position.line, end};
    viewport()->update();
}

void RbtLogTextView::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        selectWordAt(positionAt(event->position().toPoint()));
        event->accept();
        return;
    }
    QAbstractScrollArea::mouseDoubleClickEvent(event);
}

void RbtLogTextView::copySelection() const
{
    if (!hasSelection())
        return;
    TextPosition begin = m_anchor;
    TextPosition end = m_cursor;
    if (positionLess(end, begin))
        std::swap(begin, end);
    QString text;
    for (qsizetype line = begin.line; line <= end.line; ++line)
    {
        const QString current = lineText(line);
        const qsizetype first = line == begin.line ? begin.column : 0;
        const qsizetype last = line == end.line ? end.column : current.size();
        if (last > first)
            text += current.mid(first, last - first);
        if (line < end.line)
            text += QLatin1Char('\n');
    }
    QApplication::clipboard()->setText(text);
}

void RbtLogTextView::selectAllText()
{
    if (m_lineOffsets.isEmpty())
        return;
    m_anchor = {0, 0};
    const qsizetype last = m_lineOffsets.size() - 1;
    m_cursor = {last, lineText(last).size()};
    viewport()->update();
}

void RbtLogTextView::keyPressEvent(QKeyEvent* event)
{
    if (event->matches(QKeySequence::Copy))
    {
        copySelection();
        return;
    }
    if (event->matches(QKeySequence::SelectAll))
    {
        selectAllText();
        return;
    }
    if (event->matches(QKeySequence::Find))
    {
        emit findRequested();
        return;
    }
    if (event->key() == Qt::Key_F3)
    {
        emit findNextRequested(event->modifiers().testFlag(Qt::ShiftModifier));
        return;
    }
    QAbstractScrollArea::keyPressEvent(event);
}

void RbtLogTextView::contextMenuEvent(QContextMenuEvent* event)
{
    const TextPosition contextPosition = positionAt(event->pos());
    emit currentLineChanged(contextPosition.line + 1);

    QMenu menu(this);
    QAction* markCurrent = menu.addAction(QString::fromUtf8(u8"标记到当前图窗"));
    QAction* markAll = menu.addAction(QString::fromUtf8(u8"标记到所有图窗"));
    connect(markCurrent, &QAction::triggered, this, [this, contextPosition]()
    {
        emit markRequested(filePath(), contextPosition.line, false);
    });
    connect(markAll, &QAction::triggered, this, [this, contextPosition]()
    {
        emit markRequested(filePath(), contextPosition.line, true);
    });
    menu.addSeparator();
    QAction* copy = menu.addAction(QString::fromUtf8(u8"复制"));
    copy->setShortcut(QKeySequence::Copy);
    copy->setEnabled(hasSelection());
    connect(copy, &QAction::triggered, this, &RbtLogTextView::copySelection);
    QAction* selectAll = menu.addAction(QString::fromUtf8(u8"全选"));
    selectAll->setShortcut(QKeySequence::SelectAll);
    connect(selectAll, &QAction::triggered, this, &RbtLogTextView::selectAllText);
    menu.addSeparator();
    QAction* find = menu.addAction(QString::fromUtf8(u8"查找…"));
    find->setShortcut(QKeySequence::Find);
    connect(find, &QAction::triggered, this, &RbtLogTextView::findRequested);
    QAction* next = menu.addAction(QString::fromUtf8(u8"查找下一个"));
    next->setShortcut(Qt::Key_F3);
    connect(next, &QAction::triggered, this,
        [this]() { emit findNextRequested(false); });
    menu.exec(event->globalPos());
}

void RbtLogTextView::jumpToLine(qsizetype zeroBasedLine)
{
    if (m_lineOffsets.isEmpty())
        return;
    const qsizetype line = std::clamp<qsizetype>(
        zeroBasedLine, 0, m_lineOffsets.size() - 1);
    m_anchor = {line, 0};
    m_cursor = {line, lineText(line).size()};
    m_searchByteOffset = -1;
    m_searchByteLength = 0;
    verticalScrollBar()->setValue(static_cast<int>(std::max<qsizetype>(
        0, line - verticalScrollBar()->pageStep() / 3)));
    emit currentLineChanged(line + 1);
    viewport()->update();
}

quint64 RbtLogTextView::searchStartOffset(bool backward) const
{
    if (m_searchByteOffset >= 0)
    {
        return backward
            ? static_cast<quint64>(m_searchByteOffset)
            : static_cast<quint64>(m_searchByteOffset + m_searchByteLength);
    }
    return lineStart(verticalScrollBar()->value());
}

void RbtLogTextView::setSearchMatch(quint64 byteOffset, qsizetype byteLength)
{
    if (m_lineOffsets.isEmpty() || byteOffset > static_cast<quint64>(m_fileSize))
        return;
    const auto found = std::upper_bound(
        m_lineOffsets.cbegin(), m_lineOffsets.cend(), byteOffset);
    const qsizetype line = std::max<qsizetype>(0,
        static_cast<qsizetype>(found - m_lineOffsets.cbegin()) - 1);
    const quint64 start = lineStart(line);
    const qsizetype column = QString::fromUtf8(
        reinterpret_cast<const char*>(m_mapped + start),
        static_cast<qsizetype>(byteOffset - start)).size();
    const qsizetype matchCharacters = QString::fromUtf8(
        reinterpret_cast<const char*>(m_mapped + byteOffset), byteLength).size();
    m_anchor = {line, column};
    m_cursor = {line, column + std::max<qsizetype>(1, matchCharacters)};
    m_searchByteOffset = static_cast<qint64>(byteOffset);
    m_searchByteLength = byteLength;
    verticalScrollBar()->setValue(static_cast<int>(std::max<qsizetype>(
        0, line - verticalScrollBar()->pageStep() / 3)));
    emit currentLineChanged(line + 1);
    viewport()->update();
}

RbtLogViewerWindow::RbtLogViewerWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowFlag(Qt::Window, true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(QString::fromUtf8(u8"RBT 日志快速查看"));
    resize(1200, 780);

    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    auto* fileRow = new QHBoxLayout();
    fileRow->addWidget(new QLabel(QString::fromUtf8(u8"日志："), central));
    m_files = new QComboBox(central);
    m_files->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_files->setMinimumContentsLength(45);
    fileRow->addWidget(m_files, 1);
    auto* openDirectory = new QPushButton(QString::fromUtf8(u8"打开临时目录"), central);
    fileRow->addWidget(openDirectory);
    layout->addLayout(fileRow);

    auto* findRow = new QHBoxLayout();
    findRow->addWidget(new QLabel(QString::fromUtf8(u8"查找："), central));
    m_findText = new QLineEdit(central);
    m_findText->setClearButtonEnabled(true);
    findRow->addWidget(m_findText, 1);
    m_caseSensitive = new QCheckBox(QString::fromUtf8(u8"区分大小写"), central);
    findRow->addWidget(m_caseSensitive);
    m_findPrevious = new QPushButton(QString::fromUtf8(u8"上一个"), central);
    m_findNext = new QPushButton(QString::fromUtf8(u8"下一个"), central);
    findRow->addWidget(m_findPrevious);
    findRow->addWidget(m_findNext);
    layout->addLayout(findRow);

    m_textView = new RbtLogTextView(central);
    layout->addWidget(m_textView, 1);
    m_status = new QLabel(central);
    layout->addWidget(m_status);
    setCentralWidget(central);

    connect(m_files, &QComboBox::currentIndexChanged, this, [this](int index)
    {
        if (index >= 0)
            beginOpenFile(m_files->itemData(index).toString());
    });
    connect(openDirectory, &QPushButton::clicked, this, [this]()
    {
        const QString path = m_textView->filePath();
        if (!path.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });
    connect(m_findNext, &QPushButton::clicked, this, [this]() { beginFind(false); });
    connect(m_findPrevious, &QPushButton::clicked, this, [this]() { beginFind(true); });
    connect(m_findText, &QLineEdit::returnPressed, this, [this]() { beginFind(false); });
    connect(m_textView, &RbtLogTextView::findRequested, this, [this]()
    {
        m_findText->setFocus();
        m_findText->selectAll();
    });
    connect(m_textView, &RbtLogTextView::findNextRequested,
            this, &RbtLogViewerWindow::beginFind);
    connect(m_textView, &RbtLogTextView::currentLineChanged,
            this, &RbtLogViewerWindow::updateStatus);
    connect(m_textView, &RbtLogTextView::markRequested,
            this, &RbtLogViewerWindow::markRequested);
}

void RbtLogViewerWindow::openFiles(const QStringList& paths)
{
    QStringList validPaths;
    for (const QString& path : paths)
    {
        if (QFileInfo::exists(path))
            validPaths.push_back(QFileInfo(path).absoluteFilePath());
    }
    QStringList installedPaths;
    for (int index = 0; index < m_files->count(); ++index)
        installedPaths.push_back(QFileInfo(
            m_files->itemData(index).toString()).absoluteFilePath());
    if (!validPaths.isEmpty() && installedPaths == validPaths)
    {
        show();
        raise();
        activateWindow();
        return;
    }

    const QString current = m_textView->filePath();
    m_files->blockSignals(true);
    m_files->clear();
    int selected = 0;
    for (const QString& path : validPaths)
    {
        m_files->addItem(QFileInfo(path).fileName(), path);
        if (QFileInfo(path) == QFileInfo(current))
            selected = m_files->count() - 1;
    }
    m_files->setCurrentIndex(m_files->count() > 0 ? selected : -1);
    m_files->blockSignals(false);
    if (m_files->count() > 0)
        beginOpenFile(m_files->currentData().toString());
    else
    {
        m_textView->clearFile();
        m_status->setText(QString::fromUtf8(u8"临时目录中没有已解析的 RBT 日志。"));
    }
    show();
    raise();
    activateWindow();
}

void RbtLogViewerWindow::releaseFiles()
{
    ++m_openGeneration;
    ++m_findGeneration;
    m_files->blockSignals(true);
    m_files->clear();
    m_files->blockSignals(false);
    m_textView->clearFile();
    m_status->clear();
    m_pendingJumpPath.clear();
    m_pendingJumpLine = -1;
    hide();
}

bool RbtLogViewerWindow::openFileAtLine(
    const QString& path, qsizetype zeroBasedLine, QString* error)
{
    if (zeroBasedLine < 0)
    {
        if (error)
            *error = QString::fromUtf8(u8"RBT 日志行号无效。");
        return false;
    }
    if (!QFileInfo(path).isFile())
    {
        if (error)
            *error = QString::fromUtf8(u8"目标 RBT 日志文件不存在：%1").arg(path);
        return false;
    }
    if (m_textView->lineCount() > 0
        && QFileInfo(m_textView->filePath()) == QFileInfo(path))
    {
        if (zeroBasedLine >= m_textView->lineCount())
        {
            if (error)
            {
                *error = QString::fromUtf8(u8"目标行 %1 超出 RBT 日志总行数 %2。")
                             .arg(zeroBasedLine + 1)
                             .arg(m_textView->lineCount());
            }
            return false;
        }
        m_textView->jumpToLine(zeroBasedLine);
        show();
        raise();
        activateWindow();
        if (error)
            error->clear();
        emit jumpResult(QFileInfo(path).absoluteFilePath(), zeroBasedLine, true, {});
        return true;
    }

    int index = -1;
    for (int item = 0; item < m_files->count(); ++item)
    {
        if (QFileInfo(m_files->itemData(item).toString()) == QFileInfo(path))
        {
            index = item;
            break;
        }
    }
    if (index < 0)
    {
        if (error)
            *error = QString::fromUtf8(u8"目标 RBT 日志不在当前已打开的文件列表中：%1").arg(path);
        return false;
    }

    m_pendingJumpPath = QFileInfo(path).absoluteFilePath();
    m_pendingJumpLine = zeroBasedLine;
    m_files->blockSignals(true);
    m_files->setCurrentIndex(index);
    m_files->blockSignals(false);
    beginOpenFile(m_files->itemData(index).toString());
    show();
    raise();
    activateWindow();
    if (error)
        error->clear();
    return true;
}

void RbtLogViewerWindow::beginOpenFile(const QString& path)
{
    const quint64 generation = ++m_openGeneration;
    ++m_findGeneration;
    m_textView->clearFile();
    m_status->setText(QString::fromUtf8(u8"正在建立行索引：%1").arg(path));
    m_findPrevious->setEnabled(false);
    m_findNext->setEnabled(false);

    auto* watcher = new QFutureWatcher<LineIndexResult>(this);
    connect(watcher, &QFutureWatcher<LineIndexResult>::finished, this,
        [this, watcher, generation, path]()
        {
            LineIndexResult result = watcher->future().takeResult();
            watcher->deleteLater();
            if (generation != m_openGeneration)
                return;
            if (!result.error.isEmpty())
            {
                const QString reason = QString::fromUtf8(u8"无法打开日志：%1").arg(result.error);
                m_status->setText(reason);
                if (!m_pendingJumpPath.isEmpty()
                    && QFileInfo(m_pendingJumpPath) == QFileInfo(path)
                    && m_pendingJumpLine >= 0)
                {
                    const qsizetype line = m_pendingJumpLine;
                    m_pendingJumpPath.clear();
                    m_pendingJumpLine = -1;
                    emit jumpResult(path, line, false, reason);
                }
                return;
            }
            QString mapError;
            if (!m_textView->setIndexedFile(path, std::move(result.offsets), &mapError))
            {
                const QString reason = QString::fromUtf8(u8"无法映射日志：%1").arg(mapError);
                m_status->setText(reason);
                if (!m_pendingJumpPath.isEmpty()
                    && QFileInfo(m_pendingJumpPath) == QFileInfo(path)
                    && m_pendingJumpLine >= 0)
                {
                    const qsizetype line = m_pendingJumpLine;
                    m_pendingJumpPath.clear();
                    m_pendingJumpLine = -1;
                    emit jumpResult(path, line, false, reason);
                }
                return;
            }
            m_findPrevious->setEnabled(true);
            m_findNext->setEnabled(true);
            if (!m_pendingJumpPath.isEmpty()
                && QFileInfo(m_pendingJumpPath) == QFileInfo(path)
                && m_pendingJumpLine >= 0)
            {
                const qsizetype line = m_pendingJumpLine;
                if (line >= m_textView->lineCount())
                {
                    const QString reason = QString::fromUtf8(
                        u8"目标行 %1 超出 RBT 日志总行数 %2。")
                                               .arg(line + 1)
                                               .arg(m_textView->lineCount());
                    m_status->setText(reason);
                    m_pendingJumpPath.clear();
                    m_pendingJumpLine = -1;
                    emit jumpResult(path, line, false, reason);
                    return;
                }
                m_textView->jumpToLine(line);
                m_pendingJumpPath.clear();
                m_pendingJumpLine = -1;
                emit jumpResult(path, line, true, {});
            }
            updateStatus();
            m_textView->setFocus();
        });
    watcher->setFuture(QtConcurrent::run([path]() { return buildLineIndex(path); }));
}

void RbtLogViewerWindow::beginFind(bool backward)
{
    const QString query = m_findText->text();
    const QString path = m_textView->filePath();
    if (query.isEmpty() || path.isEmpty())
    {
        m_findText->setFocus();
        return;
    }
    const quint64 generation = ++m_findGeneration;
    const QByteArray needle = query.toUtf8();
    const qint64 start = static_cast<qint64>(m_textView->searchStartOffset(backward));
    const bool caseSensitive = m_caseSensitive->isChecked();
    m_findPrevious->setEnabled(false);
    m_findNext->setEnabled(false);
    m_status->setText(QString::fromUtf8(u8"正在搜索…"));

    auto* watcher = new QFutureWatcher<FindResult>(this);
    connect(watcher, &QFutureWatcher<FindResult>::finished, this,
        [this, watcher, generation, needle]()
        {
            const FindResult result = watcher->future().takeResult();
            watcher->deleteLater();
            if (generation != m_findGeneration)
                return;
            m_findPrevious->setEnabled(true);
            m_findNext->setEnabled(true);
            if (!result.error.isEmpty())
            {
                m_status->setText(QString::fromUtf8(u8"搜索失败：%1").arg(result.error));
                return;
            }
            if (result.offset < 0)
            {
                m_status->setText(QString::fromUtf8(u8"未找到“%1”。").arg(m_findText->text()));
                return;
            }
            m_textView->setSearchMatch(
                static_cast<quint64>(result.offset), needle.size());
            updateStatus();
            if (result.wrapped)
                m_status->setText(m_status->text() + QString::fromUtf8(u8"（已循环查找）"));
        });
    watcher->setFuture(QtConcurrent::run(
        [path, needle, start, backward, caseSensitive]()
        {
            return findInFile(path, needle, start, backward, caseSensitive);
        }));
}

void RbtLogViewerWindow::updateStatus(qsizetype oneBasedLine)
{
    if (m_textView->filePath().isEmpty())
        return;
    m_status->setText(QString::fromUtf8(u8"%1　%2　第 %3 / %4 行　自动换行")
        .arg(QDir::toNativeSeparators(m_textView->filePath()))
        .arg(byteCountText(m_textView->fileSize()))
        .arg(oneBasedLine)
        .arg(m_textView->lineCount()));
}
