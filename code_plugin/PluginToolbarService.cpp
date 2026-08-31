#include "code_plugin/PluginToolbarService.h"

#include "code_plugin/PluginHost.h"

#include <QAction>
#include <QApplication>
#include <QGuiApplication>
#include <QPainter>
#include <QStyleHints>
#include <QThread>
#include <QToolBar>
#include <QToolButton>

#include <algorithm>

using namespace viewer::plugin;

PluginToolbarService::PluginToolbarService(
    PluginHost* host, QToolBar* toolBar, QObject* parent)
    : QObject(parent)
    , m_host(host)
    , m_toolBar(toolBar)
{
    if (QGuiApplication::styleHints())
    {
        connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged,
                this, [this](Qt::ColorScheme)
                {
                    for (auto it = m_buttons.begin(); it != m_buttons.end(); ++it)
                        applyRecord(it.value());
                });
    }
}

PluginToolbarButtonHandle PluginToolbarService::addMenuItemButton(
    const QString& ownerPluginId,
    PluginMenuHandle menu,
    const QString& itemId,
    const PluginToolbarButtonSpec& spec)
{
    if (QThread::currentThread() != thread() || !m_host || !m_toolBar
        || ownerPluginId.trimmed().isEmpty() || !menu || itemId.trimmed().isEmpty())
    {
        return 0;
    }

    QAction* action = m_host->pluginMenuCommandAction(ownerPluginId, menu, itemId);
    if (!action)
        return 0;
    for (auto it = m_buttons.constBegin(); it != m_buttons.constEnd(); ++it)
    {
        if (it->action == action)
            return 0;
    }

    if (!m_separator)
        m_separator = m_toolBar->addSeparator();

    PluginToolbarButtonHandle handle = m_nextHandle++;
    if (!handle)
        handle = m_nextHandle++;
    ButtonRecord record;
    record.ownerPluginId = ownerPluginId;
    record.menu = menu;
    record.itemId = itemId;
    record.action = action;
    record.spec = spec;
    record.originalIcon = action->icon();
    record.originalToolTip = action->toolTip();
    m_buttons.insert(handle, std::move(record));
    rebuildOrder();
    return handle;
}

bool PluginToolbarService::updateButton(
    PluginToolbarButtonHandle button,
    const PluginToolbarButtonSpec& spec)
{
    if (QThread::currentThread() != thread())
        return false;
    auto it = m_buttons.find(button);
    if (it == m_buttons.end() || !it->action)
        return false;
    it->spec = spec;
    rebuildOrder();
    return true;
}

void PluginToolbarService::removeButton(PluginToolbarButtonHandle button)
{
    if (QThread::currentThread() != thread())
        return;
    auto it = m_buttons.find(button);
    if (it == m_buttons.end())
        return;
    const ButtonRecord record = it.value();
    m_buttons.erase(it);
    if (m_toolBar && record.action)
        m_toolBar->removeAction(record.action);
    if (record.action)
    {
        record.action->setIcon(record.originalIcon);
        record.action->setToolTip(record.originalToolTip);
    }
    removeSeparatorIfUnused();
}

void PluginToolbarService::removeOwnedButtons(const QString& ownerPluginId)
{
    QList<PluginToolbarButtonHandle> handles;
    for (auto it = m_buttons.constBegin(); it != m_buttons.constEnd(); ++it)
    {
        if (it->ownerPluginId == ownerPluginId)
            handles.push_back(it.key());
    }
    for (PluginToolbarButtonHandle handle : handles)
        removeButton(handle);
}

QIcon PluginToolbarService::resolvedIcon(
    const PluginToolbarButtonSpec& spec) const
{
    const bool dark = QGuiApplication::styleHints()
        && QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    if (dark && !spec.darkIcon.isNull())
        return spec.darkIcon;
    if (!spec.icon.isNull())
        return spec.icon;
    if (!spec.darkIcon.isNull())
        return spec.darkIcon;
    return placeholderIcon(spec.placeholderText);
}

QIcon PluginToolbarService::placeholderIcon(const QString& text) const
{
    const QSize logicalSize = m_toolBar && m_toolBar->iconSize().isValid()
        ? m_toolBar->iconSize() : QSize(36, 36);
    const qreal dpr = m_toolBar ? m_toolBar->devicePixelRatioF() : 1.0;
    QPixmap pixmap(qRound(logicalSize.width() * dpr),
                   qRound(logicalSize.height() * dpr));
    pixmap.setDevicePixelRatio(dpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QColor color = QApplication::palette().color(QPalette::ButtonText);
    QPen pen(color, 2.0, Qt::DashLine);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    QRectF rect(QPointF(3.5, 3.5),
                QSizeF(logicalSize.width() - 7.0, logicalSize.height() - 7.0));
    painter.drawRoundedRect(rect, 4.0, 4.0);

    QFont font = painter.font();
    const QString label = text.trimmed().left(4).toUpper();
    font.setBold(true);
    font.setPixelSize(label.size() > 3 ? 9 : 12);
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(QRectF(QPointF(0, 0), QSizeF(logicalSize)),
                     Qt::AlignCenter, label.isEmpty() ? QStringLiteral("P") : label);
    return QIcon(pixmap);
}

void PluginToolbarService::applyRecord(ButtonRecord& record)
{
    if (!record.action)
        return;
    record.action->setIcon(resolvedIcon(record.spec));
    record.action->setToolTip(record.spec.toolTip.isEmpty()
        ? record.originalToolTip : record.spec.toolTip);

    if (!m_toolBar)
        return;
    auto* button = qobject_cast<QToolButton*>(m_toolBar->widgetForAction(record.action));
    if (!button)
        return;
    switch (record.spec.style)
    {
    case PluginToolbarButtonStyle::IconOnly:
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        break;
    case PluginToolbarButtonStyle::TextOnly:
        button->setToolButtonStyle(Qt::ToolButtonTextOnly);
        break;
    case PluginToolbarButtonStyle::TextBesideIcon:
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        break;
    case PluginToolbarButtonStyle::TextUnderIcon:
        button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        break;
    }
    button->setText(record.spec.buttonText.isEmpty()
        ? record.action->text() : record.spec.buttonText);
    button->setStyleSheet(record.spec.styleSheet);
}

void PluginToolbarService::rebuildOrder()
{
    if (!m_toolBar)
        return;
    QList<PluginToolbarButtonHandle> handles = m_buttons.keys();
    std::sort(handles.begin(), handles.end(), [this](auto left, auto right)
    {
        const ButtonRecord& lhs = m_buttons[left];
        const ButtonRecord& rhs = m_buttons[right];
        if (lhs.spec.order != rhs.spec.order)
            return lhs.spec.order < rhs.spec.order;
        return left < right;
    });
    for (PluginToolbarButtonHandle handle : handles)
    {
        ButtonRecord& record = m_buttons[handle];
        if (record.action)
            m_toolBar->removeAction(record.action);
    }
    for (PluginToolbarButtonHandle handle : handles)
    {
        ButtonRecord& record = m_buttons[handle];
        if (!record.action)
            continue;
        m_toolBar->addAction(record.action);
        applyRecord(record);
    }
}

void PluginToolbarService::removeSeparatorIfUnused()
{
    if (!m_buttons.isEmpty() || !m_separator)
        return;
    if (m_toolBar)
        m_toolBar->removeAction(m_separator);
    delete m_separator.data();
    m_separator = nullptr;
}
