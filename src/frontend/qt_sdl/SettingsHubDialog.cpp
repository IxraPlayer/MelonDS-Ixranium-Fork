#include "SettingsHubDialog.h"
#include "CustomTitleBar.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScreen>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWindow>
#include <QResizeEvent>
#include <QShowEvent>

SettingsHubDialog::SettingsHubDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle("Settings");
    setMinimumSize(680, 480);

    // Frameless dialog: the OS decorations are dropped entirely and
    // CustomTitleBar (the same widget the main window uses) draws our own
    // titlebar with working minimize/maximize/close buttons plus drag-to-move.
    setWindowFlag(Qt::FramelessWindowHint, true);

    auto* outerV = new QVBoxLayout(this);
    outerV->setContentsMargins(0, 0, 0, 0);
    outerV->setSpacing(0);

    titleBar = new CustomTitleBar(this, this);
    titleBar->setTitleText(tr("Settings"));
    outerV->addWidget(titleBar);

    resizeGrips = new WindowResizeGrips(this);

    auto* root = new QHBoxLayout();
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    outerV->addLayout(root, 1);

    sidebar = new QListWidget(this);
    sidebar->setObjectName("sidebarPanel");
    sidebar->setFrameShape(QFrame::NoFrame);
    sidebar->setFixedWidth(220);
    sidebar->setSpacing(2);
    root->addWidget(sidebar);

    auto* right = new QVBoxLayout();
    right->setContentsMargins(24, 20, 24, 16);

    stack = new QStackedWidget;

    placeholder = new QWidget(stack);
    auto* placeholderLayout = new QVBoxLayout(placeholder);
    auto* title = new QLabel("Select a category on the left");
    title->setObjectName("libraryTitle");
    placeholderLayout->addWidget(title);
    placeholderLayout->addStretch();
    stack->addWidget(placeholder);

    // The stack sits inside a scroll area so that a page bigger than what
    // fits on screen (e.g. the input/hotkeys page) is still fully reachable
    // by scrolling, instead of forcing the window past the screen edges
    // where its bottom/right portion becomes unclickable.
    stackScroll = new QScrollArea(this);
    stackScroll->setWidgetResizable(true);
    stackScroll->setFrameShape(QFrame::NoFrame);
    stackScroll->setWidget(stack);

    right->addWidget(stackScroll, 1);

    auto* closeRow = new QHBoxLayout();
    closeRow->addStretch();
    auto* closeBtn = new QPushButton("Close");
    closeBtn->setObjectName("primaryButton");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    closeRow->addWidget(closeBtn);
    right->addLayout(closeRow);

    root->addLayout(right, 1);

    connect(sidebar, &QListWidget::itemClicked, this, &SettingsHubDialog::onItemClicked);
}

void SettingsHubDialog::paintEvent(QPaintEvent*)
{
    // See MainWindow::paintEvent() in Window.cpp for why: rounded-corner
    // masking/translucency isn't reliable on this system, so this is a
    // flat, fully opaque rectangle - no mask, no drawn corner radius.
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0x12, 0x14, 0x1a, 255));
}

void SettingsHubDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    if (resizeGrips) resizeGrips->updateGeometry();
    if (titleBar) titleBar->refreshMaximizeGlyph();
}

void SettingsHubDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
}

int SettingsHubDialog::addCategory(const QString& title)
{
    auto* item = new QListWidgetItem(title, sidebar);
    item->setSizeHint(QSize(0, 40));
    return sidebar->count() - 1;
}

void SettingsHubDialog::onItemClicked(QListWidgetItem* item)
{
    emit categorySelected(sidebar->row(item));
}

void SettingsHubDialog::setPage(QWidget* page)
{
    // Drop whatever page (other than the placeholder) is currently embedded.
    for (int i = stack->count() - 1; i >= 0; i--)
    {
        QWidget* w = stack->widget(i);
        if (w != placeholder && w != page)
        {
            stack->removeWidget(w);
            w->deleteLater();
        }
    }

    // The page must never have been shown as a top-level window before this
    // point - stripping window flags off a dialog that's already running as
    // a modal window is what caused the freeze. Callers must hand us a
    // freshly-constructed dialog that hasn't had open()/show()/exec() called.
    page->setWindowFlags(Qt::Widget);
    stack->addWidget(page);
    stack->setCurrentWidget(page);

    // The embedded page was originally designed as a standalone dialog, so
    // it carries its own preferred size. If the hub window is smaller than
    // that, the page gets squeezed into the right-hand panel and everything
    // inside it looks cramped until the user manually enlarges the window.
    // Grow (never shrink below the base minimum) to comfortably fit whatever
    // page is currently shown - but never past the screen's available area,
    // or part of the window ends up off-screen and unreachable. Anything
    // that still doesn't fit scrolls within stackScroll instead.
    QSize pageHint = page->sizeHint().expandedTo(page->minimumSizeHint());

    const int sidebarWidth = sidebar->width();
    const int rightMarginsW = 24 + 24;   // left/right content margins
    const int rightMarginsH = 20 + 16;   // top/bottom content margins
    const int closeRowH = 40;            // close button row + spacing

    int neededW = sidebarWidth + rightMarginsW + pageHint.width() + 8;
    int neededH = rightMarginsH + pageHint.height() + closeRowH;

    QSize base(680, 480);
    QSize target = base.expandedTo(QSize(neededW, neededH));

    QScreen* onScreen = screen();
    if (!onScreen)
        onScreen = QGuiApplication::primaryScreen();

    if (onScreen)
    {
        const QRect avail = onScreen->availableGeometry();
        const int margin = 40;
        QSize screenCap(avail.width() - margin, avail.height() - margin);
        target = target.boundedTo(screenCap);
    }

    if (target.width() > width() || target.height() > height())
    {
        QSize finalSize = target.expandedTo(size()).boundedTo(
            onScreen ? QSize(onScreen->availableGeometry().width() - 40,
                              onScreen->availableGeometry().height() - 40)
                     : target);
        resize(finalSize);
    }

    // Re-center the window on its screen after resizing, so switching
    // categories keeps the dialog anchored around the same middle point
    // instead of drifting toward whichever edge it happened to clamp to.
    if (onScreen)
    {
        const QRect avail = onScreen->availableGeometry();
        QRect g = geometry();
        g.moveCenter(avail.center());
        setGeometry(g);
    }
}
