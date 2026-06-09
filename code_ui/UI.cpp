#include "UI.h"

#include <qtreewidget.h>
#include <qlabel.h>

UI::UI(QWidget *parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    m_dockManager = new ads::CDockManager(ui.centralWidget);

    init();
}

UI::~UI()
{}

void UI::init()
{
    setCentralWidget(m_dockManager);

    /** Generating MainWindow part */

    ;

    /** Generating QADS part */

    ///< Center plot area
    QLabel* label = new QLabel("Plot Area");
    auto* plotDock = new ads::CDockWidget("Plot");
    plotDock->setWidget(label);
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, plotDock);

    ///< Left DateTree
    QTreeWidget* tree = new QTreeWidget();
    auto* dataDock = new ads::CDockWidget("Data");
    dataDock->setWidget(tree);
    m_dockManager->addDockWidget(ads::LeftDockWidgetArea, dataDock, plotDock->dockAreaWidget());
}
