#include "view3d_widget.h"

#include <QLabel>
#include <QVBoxLayout>

View3DWidget::View3DWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("3dview.content"));
    setMinimumSize(480, 320);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* viewport = new QLabel(QStringLiteral("3D View"), this);
    viewport->setObjectName(QStringLiteral("3dview.viewport"));
    viewport->setAlignment(Qt::AlignCenter);
    viewport->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(viewport);
}
