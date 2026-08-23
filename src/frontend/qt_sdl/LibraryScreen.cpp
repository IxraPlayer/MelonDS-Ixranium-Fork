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
#include <QConicalGradient>
#include <QElapsedTimer>
#include <QFontMetrics>
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

// Custom-painted tile: fully bypasses QToolButton's own style-based
// painting (which kept fighting the app's global .qss for the
// background) in favor of drawing everything ourselves. This is what
// makes a real ~50% translucent glass panel and the animated glow
// border possible - QSS alone can't animate, and letting QStyle draw
// its own panel on top of a custom background is what caused the
// "transparent tile showing the animated backdrop" bug earlier.
class GameCardButton : public QToolButton
{
public:
    explicit GameCardButton(bool addTileStyle, QWidget* parent)
        : QToolButton(parent), isAddTile(addTileStyle)
    {
        glowClock.start();
        auto* t = new QTimer(this);
        connect(t, &QTimer::timeout, this, [this] { update(); });
        t->start(50);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const qreal radius = 20.0;
        QRectF r = rect().adjusted(0.75, 0.75, -0.75, -0.75);
        QPainterPath path;
        path.addRoundedRect(r, radius, radius);

        // ~50% translucent glass panel, slightly brighter on hover/press.
        QColor bg = isDown()      ? QColor(30, 34, 42, 150)
                  : underMouse()  ? QColor(26, 30, 38, 145)
                                  : QColor(18, 20, 25, 128);
        painter.fillPath(path, bg);

        // Qt/QSS has no real backdrop-blur (can't blur what's actually
        // behind the widget without re-rendering it every frame), so this
        // is a practical stand-in: a soft diagonal white sheen that reads
        // as frosted glass rather than a flat translucent color.
        QLinearGradient sheen(r.topLeft(), r.bottomRight());
        sheen.setColorAt(0.0, QColor(255, 255, 255, 22));
        sheen.setColorAt(0.5, QColor(255, 255, 255, 5));
        sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter.fillPath(path, sheen);

        // Thin turquoise base border, always visible.
        painter.strokePath(path, QPen(QColor(72, 226, 226, 100), 1.1));

        // Bright white glow that slowly travels around the border, via a
        // conical gradient whose angle advances over time - cheap and
        // smooth compared to animating a path sub-segment by hand.
        double angleDeg = std::fmod(glowClock.elapsed() / 45.0, 360.0);
        QConicalGradient glow(r.center(), angleDeg);
        glow.setColorAt(0.00, QColor(255, 255, 255, 240));
        glow.setColorAt(0.06, QColor(160, 240, 240, 70));
        glow.setColorAt(0.50, QColor(90, 220, 220, 0));
        glow.setColorAt(0.94, QColor(160, 240, 240, 70));
        glow.setColorAt(1.00, QColor(255, 255, 255, 240));
        painter.strokePath(path, QPen(QBrush(glow), 1.5));

        painter.setClipPath(path);

        if (isAddTile)
        {
            QFont f = font();
            f.setPixelSize(34);
            f.setWeight(QFont::Light);
            painter.setFont(f);
            painter.setPen(underMouse() ? QColor(157, 123, 255) : QColor(90, 95, 110));
            painter.drawText(r, Qt::AlignCenter, "+");
            return;
        }

        const int iconSize = 64;
        QIcon ic = icon();
        if (!ic.isNull())
        {
            QPixmap pix = ic.pixmap(iconSize, iconSize);
            qreal ix = r.center().x() - pix.width() / 2.0;
            qreal iy = r.top() + 16;
            painter.drawPixmap(QPointF(ix, iy), pix);
        }

        QFont f = font();
        f.setPixelSize(13);
        f.setBold(true);
        painter.setFont(f);
        painter.setPen(QColor(238, 240, 245));
        QFontMetrics fm(f);
        QString elided = fm.elidedText(text(), Qt::ElideMiddle, int(r.width() - 16));
        QRectF textRect(r.left() + 8, r.bottom() - 32, r.width() - 16, 24);
        painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, elided);
    }

private:
    bool isAddTile;
    QElapsedTimer glowClock;
};

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

    addTile = new GameCardButton(true, this);
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
    bgPhase += 0.0070; // 2x speed
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

    // Water-like drift: instead of one fixed diagonal gradient (which
    // always put the turquoise in the same corner, just wobbling in
    // place), scatter a few soft turquoise/blue "blobs" that drift around
    // independently on their own lissajous-style paths, over a solid deep
    // base. Reads as moving water instead of a static banded gradient.
    QColor deep(5, 10, 18);
    painter.fillPath(path, deep);

    struct Blob { double speedX, speedY, phaseX, phaseY, rx, ry, radius; QColor color; };
    static const Blob blobs[] = {
        { 0.55, 0.40, 0.0,  1.7, 0.32, 0.30, 0.55, QColor(0, 176, 176, 150) },
        { 0.35, 0.62, 2.1,  0.4, 0.30, 0.34, 0.60, QColor(10, 120, 170, 130) },
        { 0.70, 0.28, 4.2,  3.0, 0.28, 0.26, 0.45, QColor(0, 200, 190, 110) },
    };

    const double twoPi = 6.283185307179586;
    for (const Blob& b : blobs)
    {
        double cx = r.left() + r.width()  * (0.5 + b.rx * std::sin(bgPhase * b.speedX * twoPi + b.phaseX));
        double cy = r.top()  + r.height() * (0.5 + b.ry * std::cos(bgPhase * b.speedY * twoPi + b.phaseY));
        double radius = r.width() * b.radius;

        QRadialGradient blob(QPointF(cx, cy), radius);
        blob.setColorAt(0.0, b.color);
        blob.setColorAt(1.0, QColor(b.color.red(), b.color.green(), b.color.blue(), 0));

        painter.fillPath(path, blob);
    }

    // Permanent black vignette: stays constant regardless of the bg
    // animation, darkening the corners/edges while leaving the center
    // clear so tiles/text stay readable.
    QRadialGradient vignette(r.center(), std::max(r.width(), r.height()) * 0.75);
    vignette.setColorAt(0.0, QColor(0, 0, 0, 0));
    vignette.setColorAt(0.6, QColor(0, 0, 0, 90));
    vignette.setColorAt(1.0, QColor(0, 0, 0, 220));
    painter.fillPath(path, vignette);

    QWidget::paintEvent(event);
}

QString LibraryScreen::displayName(const QString& path) const
{
    // Prefer the game's own short title (from its icon/title banner) over
    // a name derived from the filename - filenames are often full romset
    // names ("4175 - Naruto - Ninja Council 3 (Europe)(En,Fr,Es).nds")
    // that get harshly truncated on a 140px tile, whereas the banner
    // title is already the short name shown on a real DS's menu.
    QString romTitle = loadRomShortTitle(path);
    if (!romTitle.isEmpty())
        return romTitle;

    QString name = QFileInfo(path.split('|').first()).completeBaseName();
    return name;
}

QString LibraryScreen::loadRomShortTitle(const QString& path)
{
    // Archive entries aren't supported for banner extraction yet (same
    // limitation as loadRomIconImage) - let the caller fall back.
    if (path.contains('|'))
        return QString();

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    NDSHeader header;
    if (file.read(reinterpret_cast<char*>(&header), sizeof(header)) != (qint64)sizeof(header))
        return QString();

    if (header.BannerOffset == 0)
        return QString();

    char16_t titleBuf[128];
    if (!file.seek(header.BannerOffset + offsetof(NDSBanner, EnglishTitle)))
        return QString();
    if (file.read(reinterpret_cast<char*>(titleBuf), sizeof(titleBuf)) != (qint64)sizeof(titleBuf))
        return QString();

    // The banner title is up to 3 lines (game name / subtitle / publisher)
    // separated by '\n' - the first line is the actual short game name,
    // which is all we want for a tile label.
    QString full = QString::fromUtf16(reinterpret_cast<const char16_t*>(titleBuf), 128);
    int stop = full.indexOf(u'\n');
    int nul = full.indexOf(QChar(0));
    if (nul >= 0 && (stop < 0 || nul < stop))
        stop = nul;
    QString firstLine = (stop >= 0) ? full.left(stop) : full;
    firstLine = firstLine.trimmed();

    return firstLine;
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

    auto* tile = new GameCardButton(false, this);
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

void LibraryScreen::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    // Tiles are a fixed 140px with 18px grid spacing; recompute how many
    // fit across the current width so the grid actually fills the row
    // (wrapping to a new line once it can't fit another tile) instead of
    // staying locked at a fixed column count and leaving empty space on
    // the right on wider windows.
    const int tileSize = 140;
    const int spacing = 18;
    const int margins = 24 * 2;
    // Account for the scroll area's vertical scrollbar so tiles don't
    // get squeezed/wrapped early once a scrollbar appears.
    const int scrollBarAllowance = 24;

    int available = width() - margins - scrollBarAllowance;
    int newColumns = (available + spacing) / (tileSize + spacing);
    if (newColumns < 1)
        newColumns = 1;

    if (newColumns != columns)
    {
        columns = newColumns;
        relayout();
    }
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
