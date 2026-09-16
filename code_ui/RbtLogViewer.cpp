#include "RbtLogViewer.h"
#include "code_viewer/base/trace_logger.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QItemSelectionModel>
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
#include <QStyledItemDelegate>
#include <QTabWidget>
#include <QTableView>
#include <QTextLayout>
#include <QUuid>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>

namespace
{

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

QString patternFilePath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("user/rbt_match_patterns.json"));
}

void logRbtViewerTrace(const QString& message)
{
    viewer::trace::write(
        viewer::trace::Category::FileIO,
        QStringLiteral("RBT viewer | ") + message);
}

} // namespace

namespace
{

class PatternColorDelegate final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override
    {
        QStyleOptionViewItem baseOption(option);
        initStyleOption(&baseOption, index);
        baseOption.text.clear();
        QStyledItemDelegate::paint(painter, baseOption, index);

        QColor color = index.data(Qt::EditRole).value<QColor>();
        if (!color.isValid())
            return;
        QColor swatch = color;
        swatch.setAlpha(255);
        const QRect rect = option.rect.adjusted(8, 5, -8, -5);
        painter->save();
        painter->setPen(option.palette.mid().color());
        painter->setBrush(swatch);
        painter->drawRoundedRect(rect, 3, 3);
        painter->restore();
    }
};

} // namespace

RbtPatternTableModel::RbtPatternTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int RbtPatternTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rules.size());
}

int RbtPatternTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : 3;
}

QVariant RbtPatternTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rules.size())
        return {};
    const viewer::RbtPatternRule& rule = m_rules[index.row()];
    if (role == Qt::DisplayRole || role == Qt::EditRole)
    {
        switch (index.column())
        {
        case 0: return rule.name;
        case 1: return rule.expression;
        case 2: return rule.color;
        default: return {};
        }
    }
    if (role == Qt::ToolTipRole && index.column() == 2)
        return QString::fromUtf8(u8"双击选择高亮颜色");
    return {};
}

QVariant RbtPatternTableModel::headerData(
    int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);
    switch (section)
    {
    case 0: return QString::fromUtf8(u8"模式名称");
    case 1: return QString::fromUtf8(u8"模式正则表达式");
    case 2: return QString::fromUtf8(u8"高亮颜色");
    default: return {};
    }
}

Qt::ItemFlags RbtPatternTableModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::ItemIsEnabled;
    Qt::ItemFlags result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() < 2)
        result |= Qt::ItemIsEditable;
    return result;
}

bool RbtPatternTableModel::setData(
    const QModelIndex& index, const QVariant& value, int role)
{
    if (role != Qt::EditRole || !index.isValid()
        || index.row() < 0 || index.row() >= m_rules.size())
        return false;
    viewer::RbtPatternRule& rule = m_rules[index.row()];
    switch (index.column())
    {
    case 0: rule.name = value.toString(); break;
    case 1: rule.expression = value.toString(); break;
    case 2:
        if (!value.value<QColor>().isValid())
            return false;
        rule.color = value.value<QColor>();
        break;
    default: return false;
    }
    emit dataChanged(index, index, { Qt::DisplayRole, Qt::EditRole });
    return true;
}

bool RbtPatternTableModel::removeRows(
    int row, int count, const QModelIndex& parent)
{
    if (parent.isValid() || row < 0 || count <= 0 || row + count > m_rules.size())
        return false;
    beginRemoveRows({}, row, row + count - 1);
    m_rules.remove(row, count);
    endRemoveRows();
    return true;
}

void RbtPatternTableModel::setRules(QVector<viewer::RbtPatternRule> rules)
{
    beginResetModel();
    m_rules = std::move(rules);
    endResetModel();
}

void RbtPatternTableModel::addRule()
{
    viewer::RbtPatternRule rule;
    rule.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    int suffix = m_rules.size() + 1;
    do
    {
        rule.name = QString::fromUtf8(u8"新模式 %1").arg(suffix++);
    }
    while (std::any_of(m_rules.cbegin(), m_rules.cend(), [&rule](const auto& existing)
    {
        return existing.name.compare(rule.name, Qt::CaseInsensitive) == 0;
    }));
    rule.expression = QStringLiteral("ERROR|WARN");
    rule.color = QColor(255, 235, 59, 80);
    const int row = m_rules.size();
    beginInsertRows({}, row, row);
    m_rules.push_back(std::move(rule));
    endInsertRows();
}

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

void RbtLogTextView::setDocument(
    std::shared_ptr<const viewer::RbtTextDocument> document)
{
    clearFile();
    m_document = std::move(document);
    updateScrollBar();
    viewport()->update();
}

void RbtLogTextView::setMatchIndex(
    std::shared_ptr<const viewer::RbtMatchIndex> matchIndex)
{
    m_matchIndex = std::move(matchIndex);
    m_currentPatternLine = -1;
    viewport()->update();
}

void RbtLogTextView::clearFile()
{
    m_document.reset();
    m_matchIndex.reset();
    m_anchor = {};
    m_cursor = {};
    m_searchByteOffset = -1;
    m_searchByteLength = 0;
    m_currentPatternLine = -1;
    verticalScrollBar()->setRange(0, 0);
    viewport()->update();
}

qsizetype RbtLogTextView::lineCount() const noexcept
{
    return m_document ? m_document->lineCount() : 0;
}

quint64 RbtLogTextView::fileSize() const noexcept
{
    return m_document ? static_cast<quint64>(m_document->fileSize()) : 0;
}

QString RbtLogTextView::filePath() const
{
    return m_document ? m_document->filePath() : QString();
}

QString RbtLogTextView::lineText(qsizetype line) const
{
    return m_document ? m_document->lineText(line) : QString();
}

quint64 RbtLogTextView::lineStart(qsizetype line) const
{
    return m_document ? m_document->lineStart(line) : 0;
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
    if (lineCount() <= 0)
        return;

    const QFontMetrics metrics(m_textFont);
    const int digits = QString::number(lineCount()).size();
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
         lineIndex < lineCount() && y < viewport()->height(); ++lineIndex)
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

        const int ruleIndex = m_matchIndex
            ? m_matchIndex->firstRuleForLine(lineIndex) : -1;
        if (ruleIndex >= 0 && ruleIndex < m_matchIndex->rules().size())
        {
            QColor color = m_matchIndex->rules()[ruleIndex].color;
            if (color.alpha() == 255)
                color.setAlpha(80);
            painter.fillRect(QRectF(marginWidth, y,
                viewport()->width() - marginWidth, height), color);
        }
        if (lineIndex == m_currentPatternLine)
        {
            painter.fillRect(QRectF(marginWidth, y, 4, height),
                             palette().highlight());
        }

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
    if (lineCount() <= 0)
        return result;
    const QFontMetrics metrics(m_textFont);
    const int digits = QString::number(lineCount()).size();
    const int marginWidth = metrics.horizontalAdvance(QLatin1Char('9')) * digits + 18;
    const qreal textWidth = std::max(20, viewport()->width() - marginWidth - 10);
    qreal y = 0.0;
    result.line = verticalScrollBar()->value();
    for (qsizetype lineIndex = result.line; lineIndex < lineCount(); ++lineIndex)
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
        if (point.y() < y + height || lineIndex + 1 == lineCount())
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
    result.line = lineCount() - 1;
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
    if (event->button() == Qt::LeftButton && lineCount() > 0)
    {
        setFocus();
        m_anchor = m_cursor = positionAt(event->position().toPoint());
        m_currentPatternLine = -1;
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
    if (lineCount() <= 0)
        return;
    m_anchor = {0, 0};
    const qsizetype last = lineCount() - 1;
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

void RbtLogTextView::jumpToLine(qsizetype zeroBasedLine, bool selectLine)
{
    if (lineCount() <= 0)
        return;
    const qsizetype line = std::clamp<qsizetype>(
        zeroBasedLine, 0, lineCount() - 1);
    m_anchor = {line, 0};
    m_cursor = {line, selectLine ? lineText(line).size() : 0};
    m_searchByteOffset = -1;
    m_searchByteLength = 0;
    m_currentPatternLine = selectLine ? -1 : line;
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
    if (!m_document)
        return;
    qsizetype line = 0;
    qsizetype column = 0;
    qsizetype matchCharacters = 0;
    if (!m_document->textPositionForByteOffset(
            byteOffset, byteLength, &line, &column, &matchCharacters))
        return;
    m_anchor = {line, column};
    m_cursor = {line, column + std::max<qsizetype>(1, matchCharacters)};
    m_searchByteOffset = static_cast<qint64>(byteOffset);
    m_searchByteLength = byteLength;
    m_currentPatternLine = -1;
    verticalScrollBar()->setValue(static_cast<int>(std::max<qsizetype>(
        0, line - verticalScrollBar()->pageStep() / 3)));
    emit currentLineChanged(line + 1);
    viewport()->update();
}

qsizetype RbtLogTextView::navigationAnchorLine() const noexcept
{
    if (m_currentPatternLine >= 0)
        return m_currentPatternLine;
    return m_document ? m_cursor.line : -1;
}

RbtLogViewerWindow::RbtLogViewerWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowFlag(Qt::Window, true);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setAttribute(Qt::WA_QuitOnClose, false);
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

    m_tabs = new QTabWidget(central);
    m_logTab = new QWidget(m_tabs);
    auto* logLayout = new QVBoxLayout(m_logTab);
    auto* findRow = new QHBoxLayout();
    findRow->addWidget(new QLabel(QString::fromUtf8(u8"查找："), m_logTab));
    m_findText = new QLineEdit(m_logTab);
    m_findText->setClearButtonEnabled(true);
    findRow->addWidget(m_findText, 1);
    m_caseSensitive = new QCheckBox(QString::fromUtf8(u8"区分大小写"), m_logTab);
    findRow->addWidget(m_caseSensitive);
    m_findPrevious = new QPushButton(QString::fromUtf8(u8"上一个"), m_logTab);
    m_findNext = new QPushButton(QString::fromUtf8(u8"下一个"), m_logTab);
    findRow->addWidget(m_findPrevious);
    findRow->addWidget(m_findNext);
    logLayout->addLayout(findRow);

    m_textView = new RbtLogTextView(m_logTab);
    logLayout->addWidget(m_textView, 1);

    auto* navigationRow = new QHBoxLayout();
    navigationRow->addWidget(new QLabel(QString::fromUtf8(u8"导航："), m_logTab));
    m_navigationPattern = new QComboBox(m_logTab);
    m_navigationPattern->setMinimumContentsLength(18);
    navigationRow->addWidget(m_navigationPattern, 1);
    m_firstMatch = new QPushButton(QString::fromUtf8(u8"第一条"), m_logTab);
    m_previousMatch = new QPushButton(QString::fromUtf8(u8"上一条"), m_logTab);
    m_nextMatch = new QPushButton(QString::fromUtf8(u8"下一条"), m_logTab);
    m_lastMatch = new QPushButton(QString::fromUtf8(u8"最后一条"), m_logTab);
    navigationRow->addWidget(m_firstMatch);
    navigationRow->addWidget(m_previousMatch);
    navigationRow->addWidget(m_nextMatch);
    navigationRow->addWidget(m_lastMatch);
    m_matchPosition = new QLabel(m_logTab);
    m_matchPosition->setMinimumWidth(130);
    navigationRow->addWidget(m_matchPosition);
    logLayout->addLayout(navigationRow);

    m_status = new QLabel(m_logTab);
    logLayout->addWidget(m_status);
    m_tabs->addTab(m_logTab, QString::fromUtf8(u8"日志"));

    m_patternTab = new QWidget(m_tabs);
    auto* patternLayout = new QVBoxLayout(m_patternTab);
    m_patternModel = new RbtPatternTableModel(this);
    m_patternTable = new QTableView(m_patternTab);
    m_patternTable->setModel(m_patternModel);
    m_patternTable->setItemDelegateForColumn(
        2, new PatternColorDelegate(m_patternTable));
    m_patternTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_patternTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_patternTable->horizontalHeader()->setStretchLastSection(false);
    m_patternTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_patternTable->horizontalHeader()->resizeSection(0, 180);
    m_patternTable->horizontalHeader()->resizeSection(1, 720);
    m_patternTable->horizontalHeader()->resizeSection(2, 140);
    patternLayout->addWidget(m_patternTable, 1);

    auto* patternButtons = new QHBoxLayout();
    m_addPattern = new QPushButton(QString::fromUtf8(u8"新增"), m_patternTab);
    m_removePattern = new QPushButton(QString::fromUtf8(u8"删除"), m_patternTab);
    m_updatePatterns = new QPushButton(QString::fromUtf8(u8"更新"), m_patternTab);
    patternButtons->addWidget(m_addPattern);
    patternButtons->addWidget(m_removePattern);
    patternButtons->addStretch(1);
    patternButtons->addWidget(m_updatePatterns);
    patternLayout->addLayout(patternButtons);
    m_patternStatus = new QLabel(m_patternTab);
    patternLayout->addWidget(m_patternStatus);
    m_tabs->addTab(m_patternTab, QString::fromUtf8(u8"模式匹配"));
    layout->addWidget(m_tabs, 1);
    setCentralWidget(central);

    m_textManager = new viewer::RbtTextManager(this);
    QString loadError;
    if (!m_textManager->loadPatterns(patternFilePath(), &loadError))
        m_patternStatus->setText(QString::fromUtf8(u8"载入模式失败：%1").arg(loadError));
    m_patternModel->setRules(m_textManager->patterns());
    setPatternDirty(false);

    m_findPrevious->setEnabled(false);
    m_findNext->setEnabled(false);
    updateNavigationControls();

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

    connect(m_patternModel, &QAbstractItemModel::dataChanged,
            this, [this]() { setPatternDirty(true); });
    connect(m_patternModel, &QAbstractItemModel::rowsInserted,
            this, [this]() { setPatternDirty(true); });
    connect(m_patternModel, &QAbstractItemModel::rowsRemoved,
            this, [this]() { setPatternDirty(true); });
    connect(m_addPattern, &QPushButton::clicked, this, [this]()
    {
        m_patternModel->addRule();
        const int row = m_patternModel->rowCount() - 1;
        if (row >= 0)
        {
            m_patternTable->setCurrentIndex(m_patternModel->index(row, 0));
            m_patternTable->edit(m_patternModel->index(row, 0));
        }
    });
    connect(m_removePattern, &QPushButton::clicked, this, [this]()
    {
        QModelIndexList rows = m_patternTable->selectionModel()->selectedRows();
        std::sort(rows.begin(), rows.end(), [](const QModelIndex& lhs, const QModelIndex& rhs)
        {
            return lhs.row() > rhs.row();
        });
        for (const QModelIndex& row : rows)
            m_patternModel->removeRow(row.row());
    });
    connect(m_updatePatterns, &QPushButton::clicked,
            this, &RbtLogViewerWindow::applyPatternChanges);
    connect(m_patternTable, &QTableView::doubleClicked, this,
        [this](const QModelIndex& index)
        {
            if (index.column() != 2)
                return;
            QColor color = index.data(Qt::EditRole).value<QColor>();
            color = QColorDialog::getColor(color, this,
                QString::fromUtf8(u8"选择行高亮颜色"), QColorDialog::ShowAlphaChannel);
            if (color.isValid())
                m_patternModel->setData(index, color, Qt::EditRole);
        });

    connect(m_firstMatch, &QPushButton::clicked, this,
        [this]() { navigatePattern(viewer::RbtNavigateAction::First); });
    connect(m_previousMatch, &QPushButton::clicked, this,
        [this]() { navigatePattern(viewer::RbtNavigateAction::Previous); });
    connect(m_nextMatch, &QPushButton::clicked, this,
        [this]() { navigatePattern(viewer::RbtNavigateAction::Next); });
    connect(m_lastMatch, &QPushButton::clicked, this,
        [this]() { navigatePattern(viewer::RbtNavigateAction::Last); });
    connect(m_navigationPattern, &QComboBox::currentIndexChanged,
            this, [this](int) { updateNavigationControls(); });

    connect(m_textManager, &viewer::RbtTextManager::documentOpening,
        this, [this](const QString& path)
        {
            logRbtViewerTrace(QStringLiteral("document opening: path=%1 bytes=%2")
                .arg(path)
                .arg(QFileInfo(path).size()));
            m_textView->clearFile();
            m_findPrevious->setEnabled(false);
            m_findNext->setEnabled(false);
            m_status->setText(QString::fromUtf8(u8"正在建立行索引：%1").arg(path));
            updateNavigationControls();
        });
    connect(m_textManager, &viewer::RbtTextManager::documentReady,
        this, [this](const QString& path)
        {
            m_textView->setDocument(m_textManager->document());
            logRbtViewerTrace(QStringLiteral("document ready: path=%1 bytes=%2 lines=%3")
                .arg(path)
                .arg(m_textManager->document()->fileSize())
                .arg(m_textManager->document()->lineCount()));
            m_findPrevious->setEnabled(true);
            m_findNext->setEnabled(true);
            if (!m_pendingJumpPath.isEmpty()
                && QFileInfo(m_pendingJumpPath) == QFileInfo(path)
                && m_pendingJumpLine >= 0)
            {
                const qsizetype line = m_pendingJumpLine;
                m_pendingJumpPath.clear();
                m_pendingJumpLine = -1;
                if (line >= m_textView->lineCount())
                {
                    const QString reason = QString::fromUtf8(
                        u8"目标行 %1 超出 RBT 日志总行数 %2。")
                                               .arg(line + 1)
                                               .arg(m_textView->lineCount());
                    m_status->setText(reason);
                    emit jumpResult(path, line, false, reason);
                    return;
                }
                m_textView->jumpToLine(line);
                emit jumpResult(path, line, true, {});
            }
            updateStatus();
            m_textView->setFocus();
        });
    connect(m_textManager, &viewer::RbtTextManager::documentFailed,
        this, [this](const QString& path, const QString& error)
        {
            logRbtViewerTrace(QStringLiteral("document failed: path=%1 error=%2")
                .arg(path, error));
            const QString reason = QString::fromUtf8(u8"无法打开日志：%1").arg(error);
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
        });
    connect(m_textManager, &viewer::RbtTextManager::findStarted, this, [this]()
    {
        m_findPrevious->setEnabled(false);
        m_findNext->setEnabled(false);
        m_status->setText(QString::fromUtf8(u8"正在搜索…"));
    });
    connect(m_textManager, &viewer::RbtTextManager::findFinished,
        this, [this](qint64 offset, qsizetype length, bool wrapped,
                     const QString& error)
        {
            m_findPrevious->setEnabled(true);
            m_findNext->setEnabled(true);
            if (!error.isEmpty())
            {
                m_status->setText(QString::fromUtf8(u8"搜索失败：%1").arg(error));
                return;
            }
            if (offset < 0)
            {
                m_status->setText(QString::fromUtf8(u8"未找到“%1”。").arg(m_findText->text()));
                return;
            }
            m_textView->setSearchMatch(static_cast<quint64>(offset), length);
            updateStatus(m_textView->navigationAnchorLine() + 1);
            if (wrapped)
                m_status->setText(m_status->text() + QString::fromUtf8(u8"（已循环查找）"));
        });
    connect(m_textManager, &viewer::RbtTextManager::patternScanStarted,
        this, [this]()
        {
            logRbtViewerTrace(QStringLiteral("pattern scan started"));
            m_textView->setMatchIndex({});
            m_patternStatus->setText(QString::fromUtf8(u8"正在匹配当前日志：0%"));
            updateNavigationControls();
        });
    connect(m_textManager, &viewer::RbtTextManager::patternScanProgress,
        this, [this](int percent)
        {
            m_patternStatus->setText(
                QString::fromUtf8(u8"正在匹配当前日志：%1%").arg(percent));
        });
    connect(m_textManager, &viewer::RbtTextManager::patternScanFailed,
        this, [this](const QString& error)
        {
            logRbtViewerTrace(QStringLiteral("pattern scan failed: %1").arg(error));
            m_patternStatus->setText(QString::fromUtf8(u8"模式匹配失败：%1").arg(error));
            updateNavigationControls();
        });
    connect(m_textManager, &viewer::RbtTextManager::patternScanDeferred,
        this, [this](const QString& reason)
        {
            logRbtViewerTrace(QStringLiteral("automatic pattern scan deferred: %1").arg(reason));
            m_textView->setMatchIndex({});
            m_patternStatus->setText(QString::fromUtf8(
                u8"日志较大，已跳过自动规则匹配；如需匹配，请在“模式匹配”页点击“更新”。"));
            updateNavigationControls();
        });
    connect(m_textManager, &viewer::RbtTextManager::patternIndexReady,
        this, [this]()
        {
            logRbtViewerTrace(QStringLiteral("pattern index ready"));
            m_textView->setMatchIndex(m_textManager->matchIndex());
            m_patternStatus->setText(QString::fromUtf8(u8"模式匹配已更新。"));
            updateNavigationControls();
        });
}

void RbtLogViewerWindow::openFiles(const QStringList& paths)
{
    QStringList validPaths;
    for (const QString& path : paths)
    {
        if (QFileInfo::exists(path))
            validPaths.push_back(QFileInfo(path).absoluteFilePath());
    }
    quint64 totalBytes = 0;
    for (const QString& path : validPaths)
        totalBytes += static_cast<quint64>(std::max<qint64>(0, QFileInfo(path).size()));
    logRbtViewerTrace(QStringLiteral("open request: files=%1 totalBytes=%2")
        .arg(validPaths.size())
        .arg(totalBytes));

    QStringList installedPaths;
    for (int index = 0; index < m_files->count(); ++index)
        installedPaths.push_back(QFileInfo(
            m_files->itemData(index).toString()).absoluteFilePath());
    if (!validPaths.isEmpty() && installedPaths == validPaths)
    {
        presentWindow();
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
        m_textManager->clear();
        m_textView->clearFile();
        m_status->setText(QString::fromUtf8(u8"临时目录中没有已解析的 RBT 日志。"));
        updateNavigationControls();
    }
    presentWindow();
}

void RbtLogViewerWindow::releaseFiles()
{
    logRbtViewerTrace(QStringLiteral("release files"));
    m_textManager->clear();
    m_files->blockSignals(true);
    m_files->clear();
    m_files->blockSignals(false);
    m_textView->clearFile();
    m_status->clear();
    updateNavigationControls();
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
        m_tabs->setCurrentWidget(m_logTab);
        m_textView->jumpToLine(zeroBasedLine);
        presentWindow();
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
    m_tabs->setCurrentWidget(m_logTab);
    presentWindow();
    if (error)
        error->clear();
    return true;
}

void RbtLogViewerWindow::presentWindow()
{
    if (isMinimized())
        showNormal();
    else
        show();
    raise();
    activateWindow();
}

void RbtLogViewerWindow::beginOpenFile(const QString& path)
{
    if (path.isEmpty())
        return;
    logRbtViewerTrace(QStringLiteral("begin open file: %1").arg(path));
    m_textManager->openFile(path);
}

void RbtLogViewerWindow::beginFind(bool backward)
{
    const QString query = m_findText->text();
    if (query.isEmpty() || m_textView->filePath().isEmpty())
    {
        m_findText->setFocus();
        return;
    }
    const QByteArray needle = query.toUtf8();
    const qint64 start = static_cast<qint64>(m_textView->searchStartOffset(backward));
    m_textManager->find(needle, start, backward, m_caseSensitive->isChecked());
}

void RbtLogViewerWindow::applyPatternChanges()
{
    int errorRow = -1;
    QString error;
    if (!m_textManager->applyPatterns(m_patternModel->rules(), &errorRow, &error))
    {
        m_patternStatus->setText(QString::fromUtf8(u8"无法更新：%1").arg(error));
        if (errorRow >= 0)
        {
            m_patternTable->setCurrentIndex(m_patternModel->index(errorRow, 1));
            m_patternTable->scrollTo(m_patternModel->index(errorRow, 1));
        }
        return;
    }
    m_patternModel->setRules(m_textManager->patterns());
    if (!m_textManager->savePatterns(patternFilePath(), &error))
    {
        m_patternStatus->setText(QString::fromUtf8(u8"高亮已更新，但保存模式失败：%1").arg(error));
        setPatternDirty(false);
        return;
    }
    setPatternDirty(false);
    m_patternStatus->setText(m_textView->filePath().isEmpty()
        ? QString::fromUtf8(u8"模式已保存；打开日志后将自动匹配。")
        : (m_textManager->matchIndex()
            ? QString::fromUtf8(u8"模式匹配已更新。")
            : QString::fromUtf8(u8"正在匹配当前日志…")));
}

void RbtLogViewerWindow::navigatePattern(viewer::RbtNavigateAction action)
{
    const QString ruleId = m_navigationPattern->currentData().toString();
    const auto index = m_textManager->matchIndex();
    if (!index || ruleId.isEmpty())
        return;
    const viewer::RbtMatchSet* matches = index->matchesForRule(ruleId);
    if (!matches || matches->count() == 0)
        return;
    const qsizetype line = m_textManager->navigate(
        ruleId, action, m_textView->navigationAnchorLine());
    if (line < 0)
    {
        m_matchPosition->setText(action == viewer::RbtNavigateAction::Previous
            ? QString::fromUtf8(u8"已到第一条")
            : QString::fromUtf8(u8"已到最后一条"));
        return;
    }
    m_textView->jumpToLine(line, false);
    m_matchPosition->setText(QString::fromUtf8(u8"第 %1 行 / 共 %2 条")
        .arg(line + 1).arg(matches->count()));
}

void RbtLogViewerWindow::updateNavigationControls()
{
    const QString selectedId = m_navigationPattern
        ? m_navigationPattern->currentData().toString() : QString();
    const auto index = m_textManager ? m_textManager->matchIndex() : nullptr;
    if (m_navigationPattern)
    {
        m_navigationPattern->blockSignals(true);
        m_navigationPattern->clear();
        if (index)
        {
            for (const viewer::RbtPatternRule& rule : index->rules())
            {
                const viewer::RbtMatchSet* matches = index->matchesForRule(rule.id);
                m_navigationPattern->addItem(
                    QStringLiteral("%1 (%2)").arg(rule.name)
                        .arg(matches ? matches->count() : 0), rule.id);
            }
        }
        const int selected = m_navigationPattern->findData(selectedId);
        if (selected >= 0)
            m_navigationPattern->setCurrentIndex(selected);
        m_navigationPattern->blockSignals(false);
    }
    const QString ruleId = m_navigationPattern
        ? m_navigationPattern->currentData().toString() : QString();
    const viewer::RbtMatchSet* matches = index
        ? index->matchesForRule(ruleId) : nullptr;
    const bool enabled = matches && matches->count() > 0;
    if (m_firstMatch) m_firstMatch->setEnabled(enabled);
    if (m_previousMatch) m_previousMatch->setEnabled(enabled);
    if (m_nextMatch) m_nextMatch->setEnabled(enabled);
    if (m_lastMatch) m_lastMatch->setEnabled(enabled);
    if (m_navigationPattern) m_navigationPattern->setEnabled(index && !index->rules().isEmpty());
    if (m_matchPosition)
    {
        m_matchPosition->setText(enabled
            ? QString::fromUtf8(u8"共 %1 条").arg(matches->count())
            : QString::fromUtf8(u8"无匹配"));
    }
}

void RbtLogViewerWindow::setPatternDirty(bool dirty)
{
    m_patternDirty = dirty;
    if (m_tabs && m_patternTab)
    {
        const int index = m_tabs->indexOf(m_patternTab);
        if (index >= 0)
            m_tabs->setTabText(index, dirty
                ? QString::fromUtf8(u8"模式匹配 *")
                : QString::fromUtf8(u8"模式匹配"));
    }
    if (m_updatePatterns)
        m_updatePatterns->setEnabled(dirty);
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
