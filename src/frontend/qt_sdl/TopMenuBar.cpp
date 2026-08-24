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
#include <cmath>

// Fades+slides a QMenu dropdown in on open. Closing a real QMenu can't be
// delayed the way a top-level window's closeEvent can (its exec() loop
// quits the moment it's actually hidden, and ignoring hideEvent doesn't
// stop a popup from closing), so the close animation is faked: grab a
// screenshot of the menu right before it really hides, show that image in
// a tiny borrowed overlay at the same spot, and fade the overlay out while
// the real menu disappears instantly underneath it.
class MenuFadeAnimator : public QObject
{
public:
    explicit MenuFadeAnimator(QMenu* menu, QWidget* anchorBtn = nullptr)
        : QObject(menu), m_menu(menu), m_anchorBtn(anchorBtn)
    {
        m_effect = new QGraphicsOpacityEffect(menu);
        m_effect->setOpacity(1.0);
        menu->setGraphicsEffect(m_effect);
        menu->installEventFilter(this);
        connect(menu, &QMenu::aboutToHide, this, &MenuFadeAnimator::showGhost);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override
    {
        if (obj == m_menu && event->type() == QEvent::Show)
        {
            // Qt's automatic popup placement for a QToolButton menu can end
            // up wrong on this frameless/custom-titlebar window (the menu
            // was observed opening near the very top of the window instead
            // of right under its button). Rather than trust whatever Qt
            // already computed, pin the Y coordinate to just below the
            // button that owns this menu - that's the only correct answer
            // regardless of what Qt's internal heuristic decided.
            QPoint finalPos = m_menu->pos();
            if (m_anchorBtn)
                finalPos.setY(m_anchorBtn->mapToGlobal(QPoint(0, m_anchorBtn->height())).y());

            m_menu->move(finalPos.x(), finalPos.y() - 8);
            m_effect->setOpacity(0.0);

            m_posAnim = new QPropertyAnimation(m_menu, "pos", m_menu);
            m_posAnim->setDuration(150);
            m_posAnim->setStartValue(QPoint(finalPos.x(), finalPos.y() - 8));
            m_posAnim->setEndValue(finalPos);
            m_posAnim->setEasingCurve(QEasingCurve::OutCubic);
            m_posAnim->start(QAbstractAnimation::DeleteWhenStopped);

            m_opAnim = new QPropertyAnimation(m_effect, "opacity", m_menu);
            m_opAnim->setDuration(150);
            m_opAnim->setStartValue(0.0);
            m_opAnim->setEndValue(1.0);
            m_opAnim->setEasingCurve(QEasingCurve::OutCubic);
            m_opAnim->start(QAbstractAnimation::DeleteWhenStopped);
        }
        return false;
    }

private:
    void showGhost()
    {
        // If the menu is closed while its open-animation is still in
        // flight (quick click, or clicking straight to another top-bar
        // button), the position/opacity animations above keep ticking and
        // keep calling move()/repaint on the real popup window right as
        // it's being torn down - that's what read as a "shake" right at
        // close time. Cut them off first so nothing touches the real
        // menu's geometry once we've grabbed its snapshot.
        if (m_posAnim)
            m_posAnim->stop();
        if (m_opAnim)
            m_opAnim->stop();
        m_effect->setOpacity(1.0);

        if (m_menu->size().isEmpty())
            return;

        auto* ghost = new QWidget(nullptr, Qt::ToolTip | Qt::FramelessWindowHint
                                            | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint);
        ghost->setAttribute(Qt::WA_TranslucentBackground);
        ghost->setAttribute(Qt::WA_ShowWithoutActivating);
        ghost->setAttribute(Qt::WA_DeleteOnClose);
        ghost->setGeometry(m_menu->geometry());

        auto* label = new QLabel(ghost);
        label->setPixmap(m_menu->grab());
        label->setGeometry(ghost->rect());

        auto* effect = new QGraphicsOpacityEffect(ghost);
        ghost->setGraphicsEffect(effect);
        ghost->show();

        auto* anim = new QPropertyAnimation(effect, "opacity", ghost);
        anim->setDuration(120);
        anim->setStartValue(1.0);
        anim->setEndValue(0.0);
        anim->setEasingCurve(QEasingCurve::InCubic);
        connect(anim, &QPropertyAnimation::finished, ghost, &QWidget::close);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    QMenu* m_menu;
    QWidget* m_anchorBtn;
    QGraphicsOpacityEffect* m_effect;
    QPointer<QPropertyAnimation> m_posAnim;
    QPointer<QPropertyAnimation> m_opAnim;
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
        btn->setMenu(menu);
        new MenuFadeAnimator(menu, btn);

        // QToolButton keeps itself visually "pressed" (our #topMenuButton:pressed
        // blue outline) while its menu is open. When that menu gets closed
        // because the user clicked a *different* top-bar button rather than
        // this one, Qt doesn't always clear this button's sunken state - the
        // blue highlight is left stuck on until something else nudges it.
        // Force it off the moment this menu is gone.
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
