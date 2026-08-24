/*
    Copyright 2016-2026 melonDS team

    This file is part of melonDS.

    melonDS is free software: you can redistribute it and/or modify it under
    the terms of the GNU General Public License as published by the Free
    Software Foundation, either version 3 of the License, or (at your option)
    any later version.

    melonDS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with melonDS. If not, see http://www.gnu.org/licenses/.
*/

#include "TopMenuBar.h"
#include "LibraryScreen.h"

#include <QHBoxLayout>
#include <QMenu>
#include <QPropertyAnimation>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QRandomGenerator>
#include <QPixmap>
#include <QResizeEvent>
#include <QGraphicsOpacityEffect>
#include <QLabel>
#include <QEvent>
#include <QPointer>
#include <QEventLoop>
#include <cmath>

// Fades a QMenu dropdown in on open (slide+fade) and out on close.
//
// The close side used to work by grabbing a screenshot of the menu right
// before Qt actually hid it, showing that image in a second borrowed
// overlay window, and fading THAT out while the real menu vanished
// instantly underneath. In practice that second window is itself a new
// native top-level widget, and asking the OS/compositor to map and paint
// it happens asynchronously - there was no guarantee it was actually up
// on screen in the same frame the real menu disappeared in, so what got
// seen was: real menu blinks out -> (gap) -> ghost blinks in -> fades.
// That's the "gidip gelip" flicker.
//
// Instead we now fade the REAL menu in place and keep Qt from hiding it
// until the fade finishes: QMenu::aboutToHide fires synchronously, before
// Qt calls hide() on the widget, so blocking inside that slot with a tiny
// local QEventLoop - stepping the real menu's windowOpacity down while
// that loop pumps paint events - lets it visibly fade exactly where it
// already is. No second window, no timing gap.
class MenuFadeAnimator : public QObject
{
public:
    explicit MenuFadeAnimator(QMenu* menu) : QObject(menu), m_menu(menu)
    {
        menu->installEventFilter(this);
        connect(menu, &QMenu::aboutToHide, this, &MenuFadeAnimator::fadeOutAndHide);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override
    {
        if (obj == m_menu && event->type() == QEvent::Show)
        {
            const QPoint finalPos = m_menu->pos();
            m_menu->setWindowOpacity(0.0);
            m_menu->move(finalPos.x(), finalPos.y() - 8);

            m_posAnim = new QPropertyAnimation(m_menu, "pos", m_menu);
            m_posAnim->setDuration(150);
            m_posAnim->setStartValue(QPoint(finalPos.x(), finalPos.y() - 8));
            m_posAnim->setEndValue(finalPos);
            m_posAnim->setEasingCurve(QEasingCurve::OutCubic);
            m_posAnim->start(QAbstractAnimation::DeleteWhenStopped);

            m_opAnim = new QPropertyAnimation(m_menu, "windowOpacity", m_menu);
            m_opAnim->setDuration(150);
            m_opAnim->setStartValue(0.0);
            m_opAnim->setEndValue(1.0);
            m_opAnim->setEasingCurve(QEasingCurve::OutCubic);
            m_opAnim->start(QAbstractAnimation::DeleteWhenStopped);
        }
        return false;
    }

private:
    void fadeOutAndHide()
    {
        // If we're closing while the open animation is still in flight
        // (quick click), stop it first so it can't fight the fade-out
        // below or leave opacity/position in a half-finished state.
        if (m_posAnim)
            m_posAnim->stop();
        if (m_opAnim)
            m_opAnim->stop();

        if (m_reentrant)
            return;
        m_reentrant = true;

        m_menu->setWindowOpacity(1.0);

        QEventLoop loop;
        QPropertyAnimation anim(m_menu, "windowOpacity");
        anim.setDuration(120);
        anim.setStartValue(1.0);
        anim.setEndValue(0.0);
        anim.setEasingCurve(QEasingCurve::InCubic);
        connect(&anim, &QPropertyAnimation::finished, &loop, &QEventLoop::quit);
        anim.start();
        loop.exec();

        // Reset for the next time this menu opens; harmless if the menu
        // got deleted mid-loop since m_menu is only touched through the
        // still-alive QPropertyAnimation/QObject parent chain above.
        m_menu->setWindowOpacity(1.0);
        m_reentrant = false;
    }

    QMenu* m_menu;
    QPointer<QPropertyAnimation> m_posAnim;
    QPointer<QPropertyAnimation> m_opAnim;
    bool m_reentrant = false;
};

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Blue-through-turquoise only, same band used elsewhere in the UI: 0.50 =
// cyan/turquoise, 0.66 = blue. Keeps the glow line from ever drifting into
// red/green/purple.
static constexpr double kGlowHueMin = 0.50;
static constexpr double kGlowHueMax = 0.66;

// Simple single-color line-art icons drawn in code instead of emoji, so
// they're always one flat color (emoji glyphs render in their own fixed
// multi-color palette regardless of the button's text color, which looked
// out of place next to the rest of the flat UI) and can be sized freely.
static QIcon makeMenuIcon(const QString& key, const QColor& color, int size)
{
    QPixmap pix(size, size);
    pix.fill(Qt::transparent);

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, size * 0.09);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);

    const qreal s = size;

    if (key == "file")
    {
        QPainterPath path;
        path.moveTo(s * 0.15, s * 0.28);
        path.lineTo(s * 0.38, s * 0.28);
        path.lineTo(s * 0.46, s * 0.38);
        path.lineTo(s * 0.85, s * 0.38);
        path.lineTo(s * 0.85, s * 0.80);
        path.lineTo(s * 0.15, s * 0.80);
        path.closeSubpath();
        p.drawPath(path);
    }
    else if (key == "system")
    {
        p.drawRoundedRect(QRectF(s * 0.15, s * 0.20, s * 0.70, s * 0.48), s * 0.06, s * 0.06);
        p.drawLine(QPointF(s * 0.38, s * 0.84), QPointF(s * 0.62, s * 0.84));
        p.drawLine(QPointF(s * 0.50, s * 0.68), QPointF(s * 0.50, s * 0.84));
    }
    else if (key == "view")
    {
        QPainterPath path;
        path.moveTo(s * 0.12, s * 0.50);
        path.quadTo(s * 0.50, s * 0.20, s * 0.88, s * 0.50);
        path.quadTo(s * 0.50, s * 0.80, s * 0.12, s * 0.50);
        p.drawPath(path);
        p.drawEllipse(QPointF(s * 0.50, s * 0.50), s * 0.12, s * 0.12);
    }
    else if (key == "config")
    {
        // A proper gear: a ring with blocky teeth around it and a hollow
        // center, instead of the previous thin center-dot-plus-spokes
        // shape (which read more like a starburst/asterisk than a gear).
        const QPointF center(s * 0.5, s * 0.5);
        const qreal bodyR = s * 0.26;
        const qreal toothOuterR = s * 0.38;
        const qreal toothHalfWidth = s * 0.075;
        const qreal holeR = s * 0.12;

        p.setBrush(QBrush(color));
        p.setPen(Qt::NoPen);

        QPainterPath gearPath;
        gearPath.addEllipse(center, bodyR, bodyR);

        const int toothCount = 8;
        for (int i = 0; i < toothCount; i++)
        {
            qreal angle = i * (2.0 * M_PI / toothCount);
            QPointF dir(std::cos(angle), std::sin(angle));
            QPointF perp(-dir.y(), dir.x());

            QPointF base1 = center + dir * (bodyR * 0.85) + perp * toothHalfWidth;
            QPointF base2 = center + dir * (bodyR * 0.85) - perp * toothHalfWidth;
            QPointF tip1  = center + dir * toothOuterR + perp * toothHalfWidth;
            QPointF tip2  = center + dir * toothOuterR - perp * toothHalfWidth;

            QPainterPath tooth;
            tooth.moveTo(base1);
            tooth.lineTo(tip1);
            tooth.lineTo(tip2);
            tooth.lineTo(base2);
            tooth.closeSubpath();
            gearPath = gearPath.united(tooth);
        }

        QPainterPath hole;
        hole.addEllipse(center, holeR, holeR);
        gearPath = gearPath.subtracted(hole);

        p.drawPath(gearPath);
    }
    else if (key == "help")
    {
        p.drawEllipse(QRectF(s * 0.15, s * 0.15, s * 0.70, s * 0.70));
        QFont f = p.font();
        f.setBold(true);
        f.setPixelSize(int(s * 0.42));
        p.setFont(f);
        p.drawText(QRectF(0, 0, s, s), Qt::AlignCenter, "?");
    }

    return QIcon(pix);
}

// growth taken from the hovered button, split evenly and refunded by all
// its siblings so the row's total width stays constant (nothing overflows
// the centered container).
static const int kGrowAmount = 34;
static const int kAnimMs = 140;

TopMenuButton::TopMenuButton(const QString& text, QWidget* parent) : QToolButton(parent)
{
    // Pick a monochrome icon key by matching the button's title. Falls
    // back to no icon for anything unrecognized.
    QString iconKey;
    const QString lower = text.toLower();
    if (lower.contains("file"))         iconKey = "file";
    else if (lower.contains("system"))  iconKey = "system";
    else if (lower.contains("view"))    iconKey = "view";
    else if (lower.contains("config"))  iconKey = "config";
    else if (lower.contains("help"))    iconKey = "help";

    if (!iconKey.isEmpty())
    {
        const int iconSize = 26;
        setIcon(makeMenuIcon(iconKey, QColor(0xd6, 0xda, 0xe4), iconSize));
        setIconSize(QSize(iconSize, iconSize));
        setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    }
    else
    {
        setToolButtonStyle(Qt::ToolButtonTextOnly);
    }

    setText(text);
    setObjectName("topMenuButton");
    setPopupMode(QToolButton::InstantPopup);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
    setFixedWidth(m_baseWidth);
    // 54px wasn't quite enough room for icon + text together -- letters
    // with descenders (the "g" in "Config") were getting clipped against
    // the bottom edge. A little extra height (kept within the bar's own
    // 64px + reduced margins budget) plus tighter QSS padding gives the
    // text baseline the room it needs.
    setFixedHeight(55);
}

void TopMenuButton::enterEvent(MenuBtnEnterEvent* event)
{
    emit hoverChanged(this, true);
    QToolButton::enterEvent(event);
}

void TopMenuButton::leaveEvent(QEvent* event)
{
    emit hoverChanged(this, false);
    QToolButton::leaveEvent(event);
}

void TopMenuButton::mousePressEvent(QMouseEvent* event)
{
    // tiny "sinks in" nudge, purely visual, restored on release
    move(pos().x(), pos().y() + 1);

    // Menu is popped up manually (button no longer uses QToolButton's own
    // InstantPopup) and anchored explicitly right below this button. Letting
    // QToolButton trigger it itself was landing the dropdown near the top
    // of the window instead of under the button on this frameless/custom-
    // titlebar layout; calling popup() ourselves with the correct point
    // gives Qt the right anchor from the start, so its normal off-screen
    // handling (flipping above, or scrolling - see PopupCornerFixStyle)
    // works correctly too instead of us fighting it after the fact.
    if (m_dropMenu)
    {
        if (m_dropMenu->isVisible())
        {
            m_dropMenu->hide();
        }
        else
        {
            setDown(true);
            m_dropMenu->popup(mapToGlobal(QPoint(0, height())));
        }
        event->accept();
        return;
    }

    QToolButton::mousePressEvent(event);
}

void TopMenuButton::mouseReleaseEvent(QMouseEvent* event)
{
    move(pos().x(), pos().y() - 1);
    QToolButton::mouseReleaseEvent(event);
}

void TopMenuButton::animateWidthTo(int target)
{
    // We own this animation outright (no DeleteWhenStopped): stop+delete the
    // old one synchronously before making a new one, so there is never a
    // window where a stale/auto-deleted pointer could be touched again.
    if (m_anim)
    {
        m_anim->stop();
        delete m_anim;
        m_anim = nullptr;
    }

    m_anim = new QPropertyAnimation(this, "barWidth", this);
    m_anim->setDuration(kAnimMs);
    m_anim->setStartValue(width());
    m_anim->setEndValue(target);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    m_anim->start();
}

TopMenuButton::~TopMenuButton()
{
    delete m_anim;
    m_anim = nullptr;
}

TopMenuBar::TopMenuBar(QWidget* parent) : QWidget(parent),
    glowHue(0.58), glowTargetHue(0.58), glowRetargetTicks(0)
{
    setObjectName("topMenuBar");
    setFixedHeight(64);

    auto* layout = new QHBoxLayout(this);
    // Extra top margin nudges the row down a bit within the taller bar
    // instead of sitting flush against the title bar above it. Extra
    // bottom margin leaves room for the glow line under the buttons.
    layout->setContentsMargins(0, 3, 0, 6);
    layout->setSpacing(6);
    layout->addStretch(1);
    // buttons get inserted before this trailing stretch by addMenuButton()
    layout->addStretch(1);

    glowTimer = new QTimer(this);
    connect(glowTimer, &QTimer::timeout, this, [this]()
    {
        // Every ~1.8s pick a new random target hue within the band, easing
        // toward it each tick so the drift reads as smooth rather than a
        // hard jump.
        glowRetargetTicks++;
        if (glowRetargetTicks >= 45)
        {
            glowRetargetTicks = 0;
            double span = kGlowHueMax - kGlowHueMin;
            glowTargetHue = kGlowHueMin + QRandomGenerator::global()->generateDouble() * span;
        }
        glowHue += (glowTargetHue - glowHue) * 0.03;

        update();
    });
    glowTimer->start(40);

    // Small collapse arrow, bottom-right corner of the bar. Lets the
    // player fold this whole menu row away during gameplay without going
    // through a settings screen, and bring it back the same way (see
    // MainWindow's floating restore button for the other half of this).
    collapseBtn = new QToolButton(this);
    collapseBtn->setText(QString::fromUtf8("\xE2\x96\xBE")); // ▾
    collapseBtn->setToolTip(tr("Hide menu"));
    collapseBtn->setCursor(Qt::PointingHandCursor);
    collapseBtn->setFocusPolicy(Qt::NoFocus);
    collapseBtn->setFixedSize(22, 18);
    collapseBtn->setStyleSheet(
        "QToolButton { background: rgba(255,255,255,20); border: none; "
        "border-radius: 4px; color: #b8bcc8; font-size: 10px; }"
        "QToolButton:hover { background: rgba(255,255,255,45); color: white; }");
    connect(collapseBtn, &QToolButton::clicked, this, &TopMenuBar::collapseClicked);
}

void TopMenuBar::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    if (collapseBtn)
        collapseBtn->move(width() - collapseBtn->width() - 6, height() - collapseBtn->height() - 8);
}

void TopMenuBar::paintEvent(QPaintEvent* event)
{
    QWidget::paintEvent(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor lineColor = QColor::fromHsvF(std::fmod(glowHue + LibraryScreen::AccentHueShift / 360.0, 1.0), 0.70, 0.95);

    const qreal y = height() - 2.0;

    QLinearGradient gradient(0, 0, width(), 0);
    gradient.setColorAt(0.0, QColor(lineColor.red(), lineColor.green(), lineColor.blue(), 0));
    gradient.setColorAt(0.5, lineColor);
    gradient.setColorAt(1.0, QColor(lineColor.red(), lineColor.green(), lineColor.blue(), 0));

    // A few soft passes with falling alpha/width fake a glow without
    // needing a graphics effect.
    struct GlowPass { qreal width; qreal alphaScale; };
    const GlowPass passes[] = { {6.0, 0.25}, {3.0, 0.55}, {1.4, 1.0} };
    for (const auto& pass : passes)
    {
        QPen pen(QBrush(gradient), pass.width);
        pen.setCapStyle(Qt::FlatCap);
        painter.setOpacity(pass.alphaScale);
        painter.setPen(pen);
        painter.drawLine(QPointF(0, y), QPointF(width(), y));
    }
}

TopMenuButton* TopMenuBar::addMenuButton(const QString& text, QMenu* menu)
{
    auto* btn = new TopMenuButton(text, this);
    if (menu)
    {
        btn->setDropMenu(menu);
        new MenuFadeAnimator(menu);

        // The button sets itself "down" (our #topMenuButton:pressed blue
        // outline) when it opens its menu. If that menu gets closed because
        // the user clicked a *different* top-bar button rather than this
        // one, nothing else would ever clear it - force it off the moment
        // this menu is gone.
        connect(menu, &QMenu::aboutToHide, btn, [btn]()
        {
            btn->setDown(false);
            btn->update();
        });
    }

    auto* layout = static_cast<QHBoxLayout*>(this->layout());
    // insert right before the trailing stretch (last item)
    layout->insertWidget(layout->count() - 1, btn);

    buttons.append(btn);
    connect(btn, &TopMenuButton::hoverChanged, this, &TopMenuBar::onHoverChanged);

    return btn;
}

void TopMenuBar::onHoverChanged(TopMenuButton* hovered, bool isHover)
{
    int shrinkEach = buttons.size() > 1 ? kGrowAmount / (buttons.size() - 1) : 0;

    for (auto* btn : buttons)
    {
        int target = btn->baseWidth();
        if (isHover)
        {
            if (btn == hovered)
                target = btn->baseWidth() + kGrowAmount;
            else
                target = btn->baseWidth() - shrinkEach;
        }

        btn->animateWidthTo(target);
    }
}
