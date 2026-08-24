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

#ifndef TOPMENUBAR_H
#define TOPMENUBAR_H

#include <QWidget>
#include <QToolButton>
#include <QList>
#include <QTimer>
#include <QtGlobal>
#if QT_VERSION_MAJOR >= 6
#include <QEnterEvent>
#endif

class QMenu;
class QPropertyAnimation;

#if QT_VERSION_MAJOR >= 6
using MenuBtnEnterEvent = QEnterEvent;
#else
using MenuBtnEnterEvent = QEvent;
#endif

// A single top-bar menu button (File/System/View/Config/Help). Its width is
// an animatable Qt property so TopMenuBar can grow the hovered button and
// shrink its neighbours to make room for it.
class TopMenuButton : public QToolButton
{
    Q_OBJECT
    Q_PROPERTY(int barWidth READ barWidth WRITE setBarWidth)

public:
    explicit TopMenuButton(const QString& text, QWidget* parent = nullptr);

    int barWidth() const { return width(); }
    void setBarWidth(int w) { setFixedWidth(w); }

    int baseWidth() const { return m_baseWidth; }
    void setBaseWidth(int w) { m_baseWidth = w; }

    // Menu is opened manually (see mousePressEvent) instead of through
    // QToolButton's own InstantPopup, so we can hand Qt the correct anchor
    // point ourselves - see the .cpp for why.
    void setDropMenu(QMenu* menu) { m_dropMenu = menu; }

protected:
    void enterEvent(MenuBtnEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

signals:
    void hoverChanged(TopMenuButton* self, bool hovered);

private:
    int m_baseWidth = 96;
    QPropertyAnimation* m_anim = nullptr;
    QMenu* m_dropMenu = nullptr;

public:
    // Replaces any in-flight width animation on this button with a new one.
    // Owns the animation fully (no DeleteWhenStopped) so there is never a
    // dangling pointer for a caller to stop() twice.
    void animateWidthTo(int target);
    ~TopMenuButton() override;
};

// Centered row of TopMenuButtons. Hovering one animates it wider while its
// siblings animate narrower (still readable, just compact) so the whole row
// keeps its total footprint centered at the top of the window.
class TopMenuBar : public QWidget
{
    Q_OBJECT
public:
    explicit TopMenuBar(QWidget* parent = nullptr);

    // Takes ownership-by-reference: menu stays owned by whoever created it
    // (MainWindow keeps the QMenuBar alive for actions/shortcuts).
    TopMenuButton* addMenuButton(const QString& text, QMenu* menu);

signals:
    // Emitted when the small collapse arrow (bottom-right corner of the
    // bar) is clicked. MainWindow owns the actual hide/show of the
    // enclosing toolbar plus the floating "restore" button that appears
    // in its place - this widget only reports the click.
    void collapseClicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void onHoverChanged(TopMenuButton* btn, bool hovered);

    QList<TopMenuButton*> buttons;

    QToolButton* collapseBtn;

    // Thin animated blue/turquoise glow line along the bottom edge,
    // separating the menu bar from whatever's below it. Same clamped
    // hue-walk approach used elsewhere so it drifts without ever turning
    // red/green/etc.
    QTimer* glowTimer;
    double glowHue;
    double glowTargetHue;
    int glowRetargetTicks;
};

#endif // TOPMENUBAR_H
