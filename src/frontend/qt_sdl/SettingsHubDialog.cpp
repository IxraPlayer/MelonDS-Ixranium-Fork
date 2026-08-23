#include "SettingsHubDialog.h"
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

namespace
{
    // Thin drag strip along the top so the now-frameless dialog can still
    // be repositioned - same startSystemMove() approach CustomTitleBar
    // uses for the main window, just without the min/max/close buttons
    // (closing happens via the existing "Close" button at the bottom).
    class SettingsDragStrip : public QWidget
    {
    public:
        explicit SettingsDragStrip(QWidget* parent) : QWidget(parent)
        {
            setFixedHeight(30);
            auto* l = new QHBoxLayout(this);
            l->setContentsMargins(16, 0, 0, 0);
            auto* label = new QLabel(QObject::tr("Settings"), this);
            label->setStyleSheet("QLabel { color: rgba(255,255,255,190); font-size: 12px; "
                                  "font-weight: bold; background: transparent; }");
            l->addWidget(label);
            l->addStretch();
        }

    protected:
        void mousePressEvent(QMouseEvent* event) override
        {
            if (event->button() == Qt::LeftButton && window()->windowHandle())
                window()->windowHandle()->startSystemMove();
        }
    };
}

SettingsHubDialog::SettingsHubDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle("Settings");
    setMinimumSize(680, 480);

    // Frameless with rounded corners, same technique MainWindow already
    // uses for its own glassy look (see updateFramelessWindowMask() in
    // Window.cpp): opaque backing + a rounded QRegion mask, tinted via a
    // translucent fillPath in paintEvent(). This dialog never had either
    // applied before, which is why it read as a flat solid box instead of
    // matching the rest of the app's glass style.
    setWindowFlag(Qt::FramelessWindowHint, true);

    auto* outerV = new QVBoxLayout(this);
    outerV->setContentsMargins(0, 0, 0, 0);
    outerV->setSpacing(0);

    outerV->addWidget(new SettingsDragStrip(this));

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
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal radius = 14.0;
    QPainterPath path;
    path.addRoundedRect(rect(), radius, radius);

    // Genuinely translucent this time (alpha 205, not the near-opaque 235
    // the .qss themes use elsewhere) - now that WA_TranslucentBackground
    // is actually set, this blends against the desktop/game behind it
    // instead of an opaque backing store.
    painter.fillPath(path, QColor(0x12, 0x14, 0x1a, 205));
}

void SettingsHubDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);

    const int radius = 14;
    QPainterPath path;
    path.addRoundedRect(rect(), radius, radius);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
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
