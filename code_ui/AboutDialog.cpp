#include "AboutDialog.h"

#include <QFont>
#include <QFrame>
#include <QApplication>

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("关于"));
    setFixedSize(480, 240);

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(20);

    // ---- 左侧 Logo 占位 ----
    auto* logoLabel = new QLabel();
    logoLabel->setFixedSize(150, 150);
    logoLabel->setAlignment(Qt::AlignCenter);
    logoLabel->setStyleSheet(
        "QLabel { background: #e0e0e0; border: 2px dashed #aaa; border-radius: 8px; "
        "font-size: 12px; color: #888; }");
    logoLabel->setText(QStringLiteral("Logo"));
    mainLayout->addWidget(logoLabel);

    // ---- 右侧信息 ----
    auto* infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(8);

    // 软件名称（大号粗体）
    auto* titleLabel = new QLabel(QStringLiteral("Viewer V1.0"));
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    infoLayout->addWidget(titleLabel);

    // 分隔线
    auto* sep = new QFrame();
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    infoLayout->addWidget(sep);

    // 开发者
    auto* devLabel = new QLabel(QStringLiteral("By Nathan.Guan & DeepSeek V4P"));
    infoLayout->addWidget(devLabel);

    // (暂不实现) Git Commit — 待 MSBuild Target 注入宏后启用
    // auto* commitLabel = new QLabel(
    //     QStringLiteral("Git Commit: %1").arg(VIEWER_GIT_HASH));
    // commitLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // infoLayout->addWidget(commitLabel);

    // 构建日期
    auto* dateLabel = new QLabel(
        QStringLiteral("Build Date: %1 %2").arg(QString::fromLatin1(__DATE__)).arg(QString::fromLatin1(__TIME__)));
    infoLayout->addWidget(dateLabel);

    infoLayout->addStretch();

    mainLayout->addLayout(infoLayout, 1);

    // ---- 底部确定按钮 ----
    // 这里用 QVBoxLayout 包住整个布局以便添加按钮
    auto* outerLayout = new QVBoxLayout();
    outerLayout->setContentsMargins(0, 0, 0, 0);
    // 将 mainLayout 移入
    this->setLayout(nullptr);  // 移除原有布局
    QLayout* hLayout = mainLayout;
    hLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addLayout(hLayout);

    auto* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    auto* btnOk = new QPushButton(QStringLiteral("确定"));
    btnOk->setFixedWidth(80);
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(btnOk);
    outerLayout->addLayout(btnLayout);

    setLayout(outerLayout);
}