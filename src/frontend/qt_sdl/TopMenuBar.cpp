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
#include <cmath>

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
        const QPointF center(s * 0.5, s * 0.5);
        const qreal outerR = s * 0.34;
        const qreal innerR = s * 0.16;
        p.drawEllipse(center, innerR, innerR);
        for (int i = 0; i < 6; i++)
        {
            qreal angle = i * (M_PI / 3.0);
            QPointF from(center.x() + std::cos(angle) * innerR, center.y() + std::sin(angle) * innerR);
            QPointF to(center.x() + std::cos(angle) * outerR, center.y() + std::sin(angle) * outerR);
            p.drawLine(from, to);
        }
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
    setFixedHeight(54);
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
        btn->setMenu(menu);

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
