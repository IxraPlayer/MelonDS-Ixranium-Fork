#include "LibraryScreen.h"
#include <QFileInfo>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QMenu>
#include <QFile>
#include <QImage>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRadialGradient>
#include <cstddef>
#include <cmath>
#include <algorithm>

#include "NDS_Header.h"
#include <QMouseEvent>
#include <QDropEvent>
#include <QDragMoveEvent>
#include <QMimeData>
#include <QDrag>
#include <QApplication>

// MIME type used to carry the dragged tile's ROM path during a
// press-and-drag reorder within the library grid.
static const char* kGameDragMime = "application/x-melonds-game-path";

using namespace melonDS;

LibraryScreen::LibraryScreen(QWidget* parent) : QWidget(parent), columns(5), bgHue(0.58)
{
    setObjectName("libraryScreen");

    // Slow animated wave between turquoise and near-black, redrawn in
    // paintEvent(). ~20fps is plenty for something this gradual - no
    // point burning cycles animating a background that moves this slowly.
    bgAnimTimer = new QTimer(this);
    connect(bgAnimTimer, &QTimer::timeout, this, &LibraryScreen::onBgTick);
    bgAnimTimer->start(50);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 24, 24, 24);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    // Let the animated background paint straight through instead of
    // sitting behind a separately-colored opaque rectangle - that's what
    // was reading as a hard "frame" around the wave instead of a smooth
    // blend into it.
    scroll->setStyleSheet("QScrollArea { background: transparent; }");
    scroll->viewport()->setStyleSheet("background: transparent;");

    auto* inner = new QWidget();
    inner->setStyleSheet("background: transparent;");
    grid = new QGridLayout(inner);
    grid->setSpacing(18);
    grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    scroll->setWidget(inner);
    outer->addWidget(scroll);

    addTile = new QToolButton(this);
    addTile->setObjectName("addGameTile");
    addTile->setText("+");
    addTile->setFixedSize(140, 140);
    addTile->setToolButtonStyle(Qt::ToolButtonTextOnly);
    connect(addTile, &QToolButton::clicked, this, &LibraryScreen::addGameRequested);

    // The add-tile is a valid drop target (dropping a game onto it moves
    // that game to the end of the library) but is never itself draggable,
    // since it has no "romPath" property set.
    addTile->setAcceptDrops(true);
    addTile->installEventFilter(this);

    grid->addWidget(addTile, 0, 0);
}

bool LibraryScreen::eventFilter(QObject* watched, QEvent* event)
{
    QToolButton* tile = qobject_cast<QToolButton*>(watched);
    if (!tile)
        return QWidget::eventFilter(watched, event);

    switch (event->type())
    {
        case QEvent::MouseButtonPress:
        {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && tile->property("romPath").isValid())
            {
                dragStartPos = me->pos();
                dragCandidate = tile;
            }
            break;
        }

        case QEvent::MouseMove:
        {
            auto* me = static_cast<QMouseEvent*>(event);
            if (dragCandidate == tile && (me->buttons() & Qt::LeftButton) &&
                (me->pos() - dragStartPos).manhattanLength() >= QApplication::startDragDistance())
            {
                QString path = tile->property("romPath").toString();
                dragCandidate = nullptr;

                auto* mime = new QMimeData();
                mime->setData(kGameDragMime, path.toUtf8());

                auto* drag = new QDrag(tile);
                drag->setMimeData(mime);
                if (!tile->icon().isNull())
                    drag->setPixmap(tile->icon().pixmap(64, 64));
                drag->setHotSpot(QPoint(32, 32));

                // Blocks until the user drops or cancels; the actual reorder
                // happens in the Drop case below, delivered to whichever
                // tile the cursor was released over.
                drag->exec(Qt::MoveAction);

                // The button never saw its matching mouseRelease (QDrag
                // grabbed the mouse for the duration), so without this it's
                // left thinking it's still held down after the drop.
                tile->setDown(false);
                return true;
            }
            break;
        }

        case QEvent::MouseButtonRelease:
            dragCandidate = nullptr;
            break;

        case QEvent::DragEnter:
        case QEvent::DragMove:
        {
            auto* de = static_cast<QDragMoveEvent*>(event);
            if (de->mimeData()->hasFormat(kGameDragMime))
            {
                de->acceptProposedAction();
                return true;
            }
            break;
        }

        case QEvent::Drop:
        {
            auto* de = static_cast<QDropEvent*>(event);
            if (de->mimeData()->hasFormat(kGameDragMime))
            {
                QString sourcePath = QString::fromUtf8(de->mimeData()->data(kGameDragMime));
                // Empty when dropped on the "+" tile, which means "move to
                // the end of the library" rather than swap with a game.
                QString targetPath = tile->property("romPath").toString();

                if (sourcePath != targetPath && paths.contains(sourcePath))
                {
                    paths.removeAll(sourcePath);
                    int insertIndex = targetPath.isEmpty() ? paths.size() : paths.indexOf(targetPath);
                    if (insertIndex < 0)
                        insertIndex = paths.size();
                    paths.insert(insertIndex, sourcePath);
                    relayout();
                    emit libraryChanged();
                }
                de->acceptProposedAction();
                return true;
            }
            break;
        }

        default:
            break;
    }

    return QWidget::eventFilter(watched, event);
}

void LibraryScreen::onBgTick()
{
    bgPhase += 0.0035;
    if (bgPhase > 1000.0) bgPhase -= 1000.0; // keep the accumulator from growing unbounded across a long session
    update();
}

void LibraryScreen::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    // The window itself has rounded corners (see QMainWindow in the .qss),
    // so a flat square fill here left a harsh, mismatched-looking edge at
    // the bottom corners and a hard color seam against the window behind
    // it. Round the bottom corners to match the window radius and use a
    // color close to the app's base background so the seam all but
    // disappears instead of reading as a separate panel/frame.
    const qreal radius = 18.0;
    QRectF r = rect();

    QPainterPath path;
    path.moveTo(r.left(), r.top());
    path.lineTo(r.right(), r.top());
    path.lineTo(r.right(), r.bottom() - radius);
    path.arcTo(r.right() - 2 * radius, r.bottom() - 2 * radius, 2 * radius, 2 * radius, 0, -90);
    path.lineTo(r.left() + radius, r.bottom());
    path.arcTo(r.left(), r.bottom() - 2 * radius, 2 * radius, 2 * radius, -90, -90);
    path.closeSubpath();

    // Water-like drift: a diagonal gradient between deep near-black-blue
    // and turquoise, with each stop's position nudged by its own phase
    // offset+speed so the bands don't move in lockstep - reads like slow
    // moving water instead of a single sliding bar.
    QColor deep(5, 10, 18);
    QColor turquoise(0, 176, 176);
    QColor midBlue(10, 40, 70);

    QLinearGradient grad(r.topLeft(), r.bottomRight());
    auto wave = [this](double base, double speed, double amp)
    {
        const double twoPi = 6.283185307179586;
        double v = base + amp * std::sin(bgPhase * speed * twoPi);
        return std::clamp(v, 0.0, 1.0);
    };

    grad.setColorAt(0.0, deep);
    grad.setColorAt(wave(0.35, 0.6, 0.12), midBlue);
    grad.setColorAt(wave(0.65, 0.8, 0.15), turquoise);
    grad.setColorAt(1.0, deep);

    painter.fillPath(path, grad);

    // Vignette: darken toward the center so tiles/text stay readable
    // against a busy animated background, while fading out to nothing
    // near the edges - that fade is what makes the border blend smoothly
    // into the center instead of reading as two separate flat panels
    // stacked with a hard seam between them.
    QRadialGradient vignette(r.center(), std::max(r.width(), r.height()) * 0.75);
    vignette.setColorAt(0.0, QColor(6, 8, 12, 235));
    vignette.setColorAt(0.55, QColor(6, 8, 12, 190));
    vignette.setColorAt(1.0, QColor(6, 8, 12, 0));
    painter.fillPath(path, vignette);

    QWidget::paintEvent(event);
}

QString LibraryScreen::displayName(const QString& path) const
{
    QString name = QFileInfo(path.split('|').first()).completeBaseName();
    return name;
}

QImage LibraryScreen::loadRomIconImage(const QString& path)
{
    // Archive entries ("archive.zip|game.nds") aren't supported for icon
    // extraction yet; fall back to text-only tiles for those.
    if (path.contains('|'))
        return QImage();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QImage();

    NDSHeader header;
    if (file.read(reinterpret_cast<char*>(&header), sizeof(header)) != (qint64)sizeof(header))
        return QImage();

    if (header.BannerOffset == 0)
        return QImage();

    u8 iconData[512];
    u16 palette[16];

    if (!file.seek(header.BannerOffset + offsetof(NDSBanner, Icon)))
        return QImage();
    if (file.read(reinterpret_cast<char*>(iconData), sizeof(iconData)) != (qint64)sizeof(iconData))
        return QImage();

    if (!file.seek(header.BannerOffset + offsetof(NDSBanner, Palette)))
        return QImage();
    if (file.read(reinterpret_cast<char*>(palette), sizeof(palette)) != (qint64)sizeof(palette))
        return QImage();

    u32 paletteRGBA[16];
    for (int i = 0; i < 16; i++)
    {
        u8 r = ((palette[i] >> 0)  & 0x1F) * 255 / 31;
        u8 g = ((palette[i] >> 5)  & 0x1F) * 255 / 31;
        u8 b = ((palette[i] >> 10) & 0x1F) * 255 / 31;
        u8 a = i ? 255 : 0;
        paletteRGBA[i] = r | (g << 8) | (b << 16) | (a << 24);
    }

    u32 iconRGBA[32 * 32];
    int count = 0;
    for (int ytile = 0; ytile < 4; ytile++)
    {
        for (int xtile = 0; xtile < 4; xtile++)
        {
            for (int ypixel = 0; ypixel < 8; ypixel++)
            {
                for (int xpixel = 0; xpixel < 8; xpixel++)
                {
                    u8 pal_index = count % 2 ? iconData[count / 2] >> 4 : iconData[count / 2] & 0x0F;
                    iconRGBA[ytile * 256 + ypixel * 32 + xtile * 8 + xpixel] = paletteRGBA[pal_index];
                    count++;
                }
            }
        }
    }

    QImage img(reinterpret_cast<uchar*>(iconRGBA), 32, 32, QImage::Format_RGBA8888);
    return img.copy();
}

void LibraryScreen::addGame(const QString& path)
{
    if (paths.contains(path))
        return;

    paths.append(path);

    auto* tile = new QToolButton(this);
    tile->setObjectName("gameCard");
    tile->setText(displayName(path));
    tile->setFixedSize(140, 140);
    tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tile->setContextMenuPolicy(Qt::CustomContextMenu);

    // Enables press-and-drag reordering: the "romPath" property marks this
    // tile as a valid drag source (see eventFilter), and setAcceptDrops
    // makes it a valid drop target for other tiles being dragged onto it.
    tile->setProperty("romPath", path);
    tile->setAcceptDrops(true);
    tile->installEventFilter(this);

    QImage iconImg = loadRomIconImage(path);
    if (!iconImg.isNull())
    {
        QIcon icon(QPixmap::fromImage(iconImg));
        tile->setIcon(icon);
        tile->setIconSize(QSize(64, 64));
    }

    connect(tile, &QToolButton::clicked, this, [this, path]()
    {
        emit romActivated(path);
    });

    connect(tile, &QToolButton::customContextMenuRequested, this, [this, tile, path](const QPoint& pos)
    {
        QMenu menu(tile);
        QAction* removeAct = menu.addAction("Remove from library");
        QAction* chosen = menu.exec(tile->mapToGlobal(pos));
        if (chosen == removeAct)
        {
            paths.removeAll(path);
            tiles.remove(path);
            tile->deleteLater();
            relayout();
            emit libraryChanged();
        }
    });

    tiles.insert(path, tile);
    relayout();
}

void LibraryScreen::relayout()
{
    grid->removeWidget(addTile);

    int index = 0;
    for (const QString& path : paths)
    {
        QToolButton* tile = tiles.value(path, nullptr);
        if (!tile) continue;

        grid->removeWidget(tile);
        grid->addWidget(tile, index / columns, index % columns);
        index++;
    }

    grid->addWidget(addTile, index / columns, index % columns);
}
