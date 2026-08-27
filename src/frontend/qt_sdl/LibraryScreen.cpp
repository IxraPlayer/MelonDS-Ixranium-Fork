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
#include <QVariantAnimation>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QHideEvent>
#include <cstddef>
#include <cmath>
#include <algorithm>
#include <QSet>
#include <QtConcurrent>
#include <QFutureWatcher>

#include "NDS_Header.h"
#include <QMouseEvent>
#include <QDropEvent>
#include <QDragMoveEvent>
#include <QMimeData>
#include <QDrag>
#include <QApplication>
#include <QSettings>
#include <QDialog>
#include <QRadioButton>
#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QComboBox>
#include "InputConfig/ControlSchemeStore.h"
#include <QLabel>

static const char* kGameDragMime = "application/x-melonds-game-path";

static QImage scale2x(const QImage& srcIn)
{
    QImage src = srcIn.convertToFormat(QImage::Format_ARGB32);
    int w = src.width(), h = src.height();
    QImage dst(w * 2, h * 2, QImage::Format_ARGB32);

    auto at = [&](int x, int y) -> QRgb
    {
        x = std::clamp(x, 0, w - 1);
        y = std::clamp(y, 0, h - 1);
        return src.pixel(x, y);
    };

    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            QRgb B = at(x, y - 1);
            QRgb D = at(x - 1, y);
            QRgb E = at(x, y);
            QRgb F = at(x + 1, y);
            QRgb H = at(x, y + 1);

            QRgb E0 = E, E1 = E, E2 = E, E3 = E;
            if (B != H && D != F)
            {
                E0 = (D == B) ? D : E;
                E1 = (B == F) ? F : E;
                E2 = (D == H) ? D : E;
                E3 = (H == F) ? F : E;
            }

            dst.setPixel(x * 2,     y * 2,     E0);
            dst.setPixel(x * 2 + 1, y * 2,     E1);
            dst.setPixel(x * 2,     y * 2 + 1, E2);
            dst.setPixel(x * 2 + 1, y * 2 + 1, E3);
        }
    }

    return dst;
}

static QColor hueShifted(const QColor& c, int deltaDeg)
{
    if (deltaDeg == 0)
        return c;

    int h, s, l, a;
    c.getHsl(&h, &s, &l, &a);
    if (h < 0 || s == 0)
        return c; 

    h = ((h + deltaDeg) % 360 + 360) % 360;

    QColor out;
    out.setHsl(h, s, l, a);
    return out;
}

static const int kCardVisualSize = 140;
static const int kCardHoverPad = 8;
static const int kCardFootprint = kCardVisualSize + kCardHoverPad * 2;

class GameCardButton : public QToolButton
{
public:
    explicit GameCardButton(bool addTileStyle, QWidget* parent)
        : QToolButton(parent), isAddTile(addTileStyle)
    {
        glowClock.start();
        liveInstances().insert(this);
        ensureSharedTimer();

        scaleAnim = new QVariantAnimation(this);
        scaleAnim->setDuration(120);
        scaleAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(scaleAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v)
        {
            hoverScale = v.toReal();
            update();
        });
    }

    ~GameCardButton() override
    {
        liveInstances().remove(this);
    }

    // Doğrudan hazırlanmış Pixmap'i alır, ağır scale işlemlerini tamamen es geçer
    void setGameIconPixmap(const QPixmap& pix)
    {
        cachedIcon = pix;
        update();
    }

protected:
    void enterEvent(QEnterEvent*) override
    {
        raise(); 
        scaleAnim->stop();
        scaleAnim->setStartValue(hoverScale);
        scaleAnim->setEndValue(1.08);
        scaleAnim->start();
    }

    void leaveEvent(QEvent*) override
    {
        scaleAnim->stop();
        scaleAnim->setStartValue(hoverScale);
        scaleAnim->setEndValue(1.0);
        scaleAnim->start();
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        
        painter.translate(rect().center());
        painter.scale(hoverScale, hoverScale);
        painter.translate(-rect().center());

        const qreal radius = 14.0;
        QRectF r = QRectF(rect()).adjusted(kCardHoverPad + 0.75, kCardHoverPad + 0.75,
                                            -kCardHoverPad - 0.75, -kCardHoverPad - 0.75);
        QPainterPath path;
        path.addRoundedRect(r, radius, radius);

        QColor bg = isDown()      ? QColor(22, 25, 31, 150)
                  : underMouse()  ? QColor(19, 22, 28, 145)
                                  : QColor(10, 12, 16, 128);
        painter.fillPath(path, bg);

        QLinearGradient sheen(r.topLeft(), r.bottomRight());
        sheen.setColorAt(0.0, QColor(255, 255, 255, 22));
        sheen.setColorAt(0.5, QColor(255, 255, 255, 5));
        sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter.fillPath(path, sheen);

        painter.strokePath(path, QPen(hueShifted(QColor(72, 226, 226, 100), LibraryScreen::AccentHueShift), 1.1));

        double angleDeg = std::fmod(glowClock.elapsed() / 45.0, 360.0);
        QConicalGradient glow(r.center(), angleDeg);
        glow.setColorAt(0.00, QColor(255, 255, 255, 240));
        glow.setColorAt(0.06, hueShifted(QColor(160, 240, 240, 70), LibraryScreen::AccentHueShift));
        glow.setColorAt(0.50, hueShifted(QColor(90, 220, 220, 0), LibraryScreen::AccentHueShift));
        glow.setColorAt(0.94, hueShifted(QColor(160, 240, 240, 70), LibraryScreen::AccentHueShift));
        glow.setColorAt(1.00, QColor(255, 255, 255, 240));
        painter.strokePath(path, QPen(QBrush(glow), 1.5));

        painter.setClipPath(path);

        if (isAddTile)
        {
            QFont f = font();
            f.setPixelSize(34);
            f.setWeight(QFont::Light);
            painter.setFont(f);
            painter.setPen(underMouse() ? hueShifted(QColor(123, 255, 255), LibraryScreen::AccentHueShift) : QColor(90, 95, 110));
            painter.drawText(r, Qt::AlignCenter, "+");
            return;
        }

        if (!cachedIcon.isNull())
        {
            const QPixmap& pix = cachedIcon;
            QRectF iconArea(r.left(), r.top() + 12, r.width(), (r.bottom() - 32) - (r.top() + 12));
            qreal ix = iconArea.center().x() - pix.width() / 2.0;
            qreal iy = iconArea.center().y() - pix.height() / 2.0;

            QPainterPath iconClip;
            iconClip.addRoundedRect(QRectF(ix, iy, pix.width(), pix.height()), 15, 15);
            painter.save();
            painter.setClipPath(iconClip, Qt::IntersectClip);
            painter.drawPixmap(QPointF(ix, iy), pix);
            painter.restore();
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
    static QSet<GameCardButton*>& liveInstances()
    {
        static QSet<GameCardButton*> instances;
        return instances;
    }

    static void ensureSharedTimer()
    {
        static QTimer* timer = nullptr;
        if (timer)
            return;
        timer = new QTimer(qApp);
        QObject::connect(timer, &QTimer::timeout, qApp, []()
        {
            for (GameCardButton* w : liveInstances())
            {
                if ((w->underMouse() || w->isDown()) &&
                    w->isVisible() && !w->visibleRegion().isEmpty())
                {
                    w->update();
                }
            }
        });
        timer->start(120);
    }

    bool isAddTile;
    QElapsedTimer glowClock;
    QVariantAnimation* scaleAnim = nullptr;
    qreal hoverScale = 1.0;
    QPixmap cachedIcon;
};

using namespace melonDS;

int LibraryScreen::AccentHueShift = 0;

void LibraryScreen::ApplyAccentTheme(const QString& qssThemeName)
{
    if (qssThemeName == "ixranium_red")
        AccentHueShift = 180;
    else if (qssThemeName == "ixranium_green")
        AccentHueShift = 300;
    else if (qssThemeName == "ixranium_purple")
        AccentHueShift = 100;
    else if (qssThemeName == "ixranium_blue")
        AccentHueShift = 60;
    else
        AccentHueShift = 0; 
}

LibraryScreen::LibraryScreen(QWidget* parent) : QWidget(parent), columns(5), bgHue(0.58)
{
    setObjectName("libraryScreen");

    loadConsoleOverrides();
    loadSchemeOverrides();

    bgAnimTimer = new QTimer(this);
    connect(bgAnimTimer, &QTimer::timeout, this, &LibraryScreen::onBgTick);
    bgAnimTimer->start(50);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(24, 24, 24, 24);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background: transparent; }");
    scroll->viewport()->setStyleSheet("background: transparent;");

    auto* inner = new QWidget();
    inner->setStyleSheet("background: transparent;");
    grid = new QGridLayout(inner);
    grid->setSpacing(18 - 2 * kCardHoverPad);
    grid->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    scroll->setWidget(inner);
    outer->addWidget(scroll);

    addTile = new GameCardButton(true, this);
    addTile->setObjectName("addGameTile");
    addTile->setText("+");
    addTile->setFixedSize(kCardFootprint, kCardFootprint);
    addTile->setToolButtonStyle(Qt::ToolButtonTextOnly);
    connect(addTile, &QToolButton::clicked, this, &LibraryScreen::addGameRequested);

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

                drag->exec(Qt::MoveAction);
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

void LibraryScreen::rebuildVignetteCache(const QRectF& r, const QPainterPath& path)
{
    vignetteCacheSize = size();
    vignetteCache = QPixmap(vignetteCacheSize);
    vignetteCache.fill(Qt::transparent);

    QPainter p(&vignetteCache);
    p.setRenderHint(QPainter::Antialiasing, true);

    QRadialGradient vignette(r.center(), std::max(r.width(), r.height()) * 0.75);
    vignette.setColorAt(0.0, QColor(0, 0, 0, 0));
    vignette.setColorAt(0.6, QColor(0, 0, 0, 90));
    vignette.setColorAt(1.0, QColor(0, 0, 0, 220));
    p.fillPath(path, vignette);
}

void LibraryScreen::showEvent(QShowEvent* event)
{
    if (bgAnimTimer && !bgAnimTimer->isActive())
        bgAnimTimer->start(50);
    QWidget::showEvent(event);
}

void LibraryScreen::hideEvent(QHideEvent* event)
{
    if (bgAnimTimer)
        bgAnimTimer->stop();
    QWidget::hideEvent(event);
}

void LibraryScreen::onBgTick()
{
    bgPhase += 0.0070; 
    if (bgPhase > 1000.0) bgPhase -= 1000.0; 
    update();
}

void LibraryScreen::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const qreal radius = 0.0;
    QRectF r = rect();

    QPainterPath path;
    path.moveTo(r.left(), r.top());
    path.lineTo(r.right(), r.top());
    path.lineTo(r.right(), r.bottom() - radius);
    path.arcTo(r.right() - 2 * radius, r.bottom() - 2 * radius, 2 * radius, 2 * radius, 0, -90);
    path.lineTo(r.left() + radius, r.bottom());
    path.arcTo(r.left(), r.bottom() - 2 * radius, 2 * radius, 2 * radius, -90, -90);
    path.closeSubpath();

    QColor deep = hueShifted(QColor(2, 11, 13), AccentHueShift);
    painter.fillPath(path, deep);

    struct Blob { double speedX, speedY, phaseX, phaseY, rx, ry, radius; QColor color; };
    static const Blob blobs[] = {
        { 0.55, 0.40, 0.0,  1.7, 0.34, 0.32, 0.62, QColor(0, 83, 83, 95) },     
        { 0.35, 0.62, 2.1,  0.4, 0.32, 0.36, 0.66, QColor(10, 116, 116, 100) }, 
        { 0.70, 0.28, 4.2,  3.0, 0.30, 0.28, 0.52, QColor(0, 74, 74, 80) },    
        { 0.46, 0.50, 1.1,  5.0, 0.36, 0.30, 0.58, QColor(18, 120, 120, 85) },  
        { 0.60, 0.33, 3.4,  0.9, 0.28, 0.34, 0.48, QColor(0, 59, 64, 90) },    
    };

    painter.setCompositionMode(QPainter::CompositionMode_Plus);

    const double twoPi = 6.283185307179586;
    for (const Blob& b : blobs)
    {
        double cx = r.left() + r.width()  * (0.5 + b.rx * std::sin(bgPhase * b.speedX * twoPi + b.phaseX));
        double cy = r.top()  + r.height() * (0.5 + b.ry * std::cos(bgPhase * b.speedY * twoPi + b.phaseY));
        double radius = r.width() * b.radius;

        QColor color = hueShifted(b.color, AccentHueShift);
        QRadialGradient blob(QPointF(cx, cy), radius);
        blob.setColorAt(0.0, color);
        blob.setColorAt(0.6, QColor(color.red(), color.green(), color.blue(), color.alpha() / 2));
        blob.setColorAt(1.0, QColor(color.red(), color.green(), color.blue(), 0));

        painter.fillPath(path, blob);
    }

    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    if (vignetteCache.isNull() || vignetteCacheSize != size())
        rebuildVignetteCache(r, path);
    painter.drawPixmap(0, 0, vignetteCache);

    QWidget::paintEvent(event);
}

QString LibraryScreen::displayName(const QString& path) const
{
    QString romTitle = loadRomShortTitle(path);
    if (!romTitle.isEmpty())
        return romTitle;

    QString name = QFileInfo(path.split('|').first()).completeBaseName();
    return name;
}

QString LibraryScreen::loadRomShortTitle(const QString& path)
{
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
    tile->setFixedSize(kCardFootprint, kCardFootprint);
    tile->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tile->setContextMenuPolicy(Qt::CustomContextMenu);

    tile->setProperty("romPath", path);
    tile->setAcceptDrops(true);
    tile->installEventFilter(this);

    // [Asenkron Optimizasyon]
    // İkon okuma ve scale2x işlemleri arka plan iş parçacığına devredildi. 
    // Ana kilitlenmeler ve kasmalar önlendi!
    QFutureWatcher<QImage>* watcher = new QFutureWatcher<QImage>(tile);
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [tile, watcher]() {
        QImage img = watcher->result();
        if (!img.isNull()) {
            tile->setGameIconPixmap(QPixmap::fromImage(img));
        }
        watcher->deleteLater();
    });
    
    watcher->setFuture(QtConcurrent::run([path]() -> QImage {
        QImage iconImg = loadRomIconImage(path);
        if (iconImg.isNull()) return QImage();

        const int iconSize = 48;
        QImage scaled = scale2x(scale2x(iconImg));
        if (scaled.width() > iconSize || scaled.height() > iconSize)
            scaled = scaled.scaled(iconSize, iconSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        return scaled;
    }));

    connect(tile, &QToolButton::clicked, this, [this, path]()
    {
        emit romActivated(path);
    });

    connect(tile, &QToolButton::customContextMenuRequested, this, [this, tile, path](const QPoint& pos)
    {
        QMenu menu(tile);
        menu.setAttribute(Qt::WA_TranslucentBackground, true);
        menu.setStyleSheet(
            "QMenu { background: rgba(24,26,34,255); border: 1px solid rgba(255,255,255,30); "
            "  border-radius: 8px; padding: 4px; color: white; }"
            "QMenu::item { padding: 6px 14px; border-radius: 5px; }"
            "QMenu::item:selected { background: rgba(255,255,255,35); }");
        QAction* detailsAct = menu.addAction("Details...");
        QAction* removeAct = menu.addAction("Remove from library");
        QAction* chosen = menu.exec(tile->mapToGlobal(pos));

        if (!tile->underMouse())
        {
            QEvent leave(QEvent::Leave);
            QApplication::sendEvent(tile, &leave);
        }

        if (chosen == detailsAct)
        {
            showGameDetailsDialog(path, tile);
        }
        else if (chosen == removeAct)
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

static const char* kConsoleOverrideSettingsGroup = "GameConsoleOverrides";

void LibraryScreen::loadConsoleOverrides()
{
    QSettings settings;
    settings.beginGroup(kConsoleOverrideSettingsGroup);
    for (const QString& key : settings.childKeys())
    {
        int val = settings.value(key).toInt();
        if (val == 0 || val == 1)
            consoleOverrides.insert(key, val);
    }
    settings.endGroup();
}

void LibraryScreen::saveConsoleOverrides()
{
    QSettings settings;
    settings.beginGroup(kConsoleOverrideSettingsGroup);
    settings.remove(""); 
    for (auto it = consoleOverrides.constBegin(); it != consoleOverrides.constEnd(); ++it)
        settings.setValue(it.key(), it.value());
    settings.endGroup();
}

int LibraryScreen::consoleTypeOverride(const QString& path) const
{
    return consoleOverrides.value(path, -1);
}

void LibraryScreen::setConsoleTypeOverride(const QString& path, int type)
{
    if (type == 0 || type == 1)
        consoleOverrides.insert(path, type);
    else
        consoleOverrides.remove(path);
    saveConsoleOverrides();
}

static const char* kSchemeOverrideSettingsGroup = "GameControlSchemeOverrides";

void LibraryScreen::loadSchemeOverrides()
{
    QSettings settings;
    settings.beginGroup(kSchemeOverrideSettingsGroup);
    for (const QString& key : settings.childKeys())
    {
        QString val = settings.value(key).toString();
        if (!val.isEmpty())
            schemeOverrides.insert(key, val);
    }
    settings.endGroup();
}

void LibraryScreen::saveSchemeOverrides()
{
    QSettings settings;
    settings.beginGroup(kSchemeOverrideSettingsGroup);
    settings.remove("");
    for (auto it = schemeOverrides.constBegin(); it != schemeOverrides.constEnd(); ++it)
        settings.setValue(it.key(), it.value());
    settings.endGroup();
}

QString LibraryScreen::controlSchemeOverride(const QString& path) const
{
    return schemeOverrides.value(path, QString());
}

void LibraryScreen::setControlSchemeOverride(const QString& path, const QString& schemeName)
{
    if (!schemeName.isEmpty())
        schemeOverrides.insert(path, schemeName);
    else
        schemeOverrides.remove(path);
    saveSchemeOverrides();
}

void LibraryScreen::showGameDetailsDialog(const QString& path, QWidget* anchor)
{
    QDialog dlg(anchor ? anchor->window() : this);
    dlg.setWindowTitle("Game Details");
    dlg.setStyleSheet(
        "QDialog { background: rgb(24,26,34); color: white; }"
        "QLabel { color: white; }"
        "QRadioButton { color: white; padding: 4px; }");

    auto* layout = new QVBoxLayout(&dlg);

    auto* nameLabel = new QLabel(displayName(path), &dlg);
    QFont nf = nameLabel->font();
    nf.setBold(true);
    nameLabel->setFont(nf);
    layout->addWidget(nameLabel);

    auto* consoleLabel = new QLabel("Console type for this game:", &dlg);
    layout->addWidget(consoleLabel);

    auto* group = new QButtonGroup(&dlg);
    auto* optDefault = new QRadioButton("Use global default", &dlg);
    auto* optDS = new QRadioButton("Nintendo DS", &dlg);
    auto* optDSi = new QRadioButton("Nintendo DSi", &dlg);
    group->addButton(optDefault, -1);
    group->addButton(optDS, 0);
    group->addButton(optDSi, 1);
    layout->addWidget(optDefault);
    layout->addWidget(optDS);
    layout->addWidget(optDSi);

    int current = consoleTypeOverride(path);
    if (current == 0) optDS->setChecked(true);
    else if (current == 1) optDSi->setChecked(true);
    else optDefault->setChecked(true);

    auto* schemeLabel = new QLabel("Kontrol şeması:", &dlg);
    layout->addWidget(schemeLabel);

    auto* cbxScheme = new QComboBox(&dlg);
    cbxScheme->addItem("Global (varsayılan)");
    QStringList schemeNames = ControlSchemeStore::listNames();
    cbxScheme->addItems(schemeNames);
    layout->addWidget(cbxScheme);

    QString currentScheme = controlSchemeOverride(path);
    int schemeIdx = currentScheme.isEmpty() ? 0 : (1 + schemeNames.indexOf(currentScheme));
    cbxScheme->setCurrentIndex(schemeIdx < 1 ? 0 : schemeIdx);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    if (dlg.exec() == QDialog::Accepted)
    {
        setConsoleTypeOverride(path, group->checkedId());

        int idx = cbxScheme->currentIndex();
        setControlSchemeOverride(path, idx <= 0 ? QString() : schemeNames[idx - 1]);
    }
}

void LibraryScreen::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    const int tileSize = kCardFootprint;
    const int spacing = 18 - 2 * kCardHoverPad;
    const int margins = 24 * 2;
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
    // [Relayout Titreme Optimizasyonu]
    // Widget'ları silip eklemek yerine sadece değişmesi gerekenlerin
    // pozisyonunu grid içinde güncelleyerek anlık donmaların kökünü kazıdık.
    int index = 0;
    for (const QString& path : paths)
    {
        QToolButton* tile = tiles.value(path, nullptr);
        if (!tile) continue;

        int targetRow = index / columns;
        int targetCol = index % columns;

        int curRow = -1, curCol = -1, rs = -1, cs = -1;
        int gridIndex = grid->indexOf(tile);
        
        if (gridIndex != -1) {
            grid->getItemPosition(gridIndex, &curRow, &curCol, &rs, &cs);
        }

        if (curRow != targetRow || curCol != targetCol) {
            grid->addWidget(tile, targetRow, targetCol);
        }
        index++;
    }

    int targetRow = index / columns;
    int targetCol = index % columns;
    int curRow = -1, curCol = -1, rs = -1, cs = -1;
    
    int addGridIndex = grid->indexOf(addTile);
    if (addGridIndex != -1) {
        grid->getItemPosition(addGridIndex, &curRow, &curCol, &rs, &cs);
    }
    
    if (curRow != targetRow || curCol != targetCol) {
        grid->addWidget(addTile, targetRow, targetCol);
    }
}
