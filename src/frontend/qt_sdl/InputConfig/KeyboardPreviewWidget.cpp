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

#include "KeyboardPreviewWidget.h"
#include "InputConfigDialog.h" // hk_general/hk_addons + their display labels

#include <QPainter>
#include <QHelpEvent>
#include <QToolTip>
#include <QStringList>
#include <algorithm>

#include "Config.h"
#include "EmuInstance.h"

// Bits Qt (and this app's own KeyMapButton convention) can OR into a stored
// key mapping on top of the base Qt::Key value.
static const unsigned kModMask =
    Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier |
    Qt::MetaModifier | Qt::KeypadModifier | Qt::GroupSwitchModifier |
    0x80000000u; // "right-hand modifier key" bit, see MapButton.h

static int baseKeyOf(int stored)  { return (int)((unsigned)stored & ~kModMask); }
static bool isNumpadOf(int stored){ return ((unsigned)stored & Qt::KeypadModifier) != 0; }

// Small helper: one row of keys, each "label|widthUnits|Qt::Key" triple,
// packed as {label, width, key}. width is in "1u = one normal keycap".
struct RowKey { const char* label; double w; int key; bool numpad; };

KeyboardPreviewWidget::KeyboardPreviewWidget(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    buildLayout();
}

QSize KeyboardPreviewWidget::sizeHint() const
{
    return QSize(430, 150);
}

void KeyboardPreviewWidget::buildLayout()
{
    cells.clear();

    const double unit = 1.0; // abstract units, scaled to widget size at paint time
    double y = 0;
    double mainWidth = 0;

    auto addRow = [&](std::initializer_list<RowKey> keys, double startX = 0)
    {
        double x = startX;
        for (const RowKey& k : keys)
        {
            KeyCell c;
            c.rect = QRect(); // filled below via scaling info stored as float rect
            c.label = QString::fromUtf8(k.label);
            c.qtKey = k.key;
            // stash raw (unscaled) geometry in rect using unit*100 so int QRect keeps precision
            c.rect = QRect(int(x*100), int(y*100), int(k.w*100)-4, 92);
            cells.push_back(c);
            x += k.w;
        }
        if (x > mainWidth) mainWidth = x;
        y += 1.0;
    };

    // --- Main block ---
    addRow({ {"Esc",1,Qt::Key_Escape,false},{"1",1,Qt::Key_1,false},{"2",1,Qt::Key_2,false},{"3",1,Qt::Key_3,false},
              {"4",1,Qt::Key_4,false},{"5",1,Qt::Key_5,false},{"6",1,Qt::Key_6,false},{"7",1,Qt::Key_7,false},
              {"8",1,Qt::Key_8,false},{"9",1,Qt::Key_9,false},{"0",1,Qt::Key_0,false},{"-",1,Qt::Key_Minus,false},
              {"=",1,Qt::Key_Equal,false},{"Bksp",2,Qt::Key_Backspace,false} });

    addRow({ {"Tab",1.5,Qt::Key_Tab,false},{"Q",1,Qt::Key_Q,false},{"W",1,Qt::Key_W,false},{"E",1,Qt::Key_E,false},
              {"R",1,Qt::Key_R,false},{"T",1,Qt::Key_T,false},{"Y",1,Qt::Key_Y,false},{"U",1,Qt::Key_U,false},
              {"I",1,Qt::Key_I,false},{"O",1,Qt::Key_O,false},{"P",1,Qt::Key_P,false},{"[",1,Qt::Key_BracketLeft,false},
              {"]",1,Qt::Key_BracketRight,false},{"\\",1.5,Qt::Key_Backslash,false} });

    addRow({ {"Caps",1.75,Qt::Key_CapsLock,false},{"A",1,Qt::Key_A,false},{"S",1,Qt::Key_S,false},{"D",1,Qt::Key_D,false},
              {"F",1,Qt::Key_F,false},{"G",1,Qt::Key_G,false},{"H",1,Qt::Key_H,false},{"J",1,Qt::Key_J,false},
              {"K",1,Qt::Key_K,false},{"L",1,Qt::Key_L,false},{";",1,Qt::Key_Semicolon,false},{"'",1,Qt::Key_Apostrophe,false},
              {"Enter",2.25,Qt::Key_Return,false} });

    addRow({ {"Shift",2.25,Qt::Key_Shift,false},{"Z",1,Qt::Key_Z,false},{"X",1,Qt::Key_X,false},{"C",1,Qt::Key_C,false},
              {"V",1,Qt::Key_V,false},{"B",1,Qt::Key_B,false},{"N",1,Qt::Key_N,false},{"M",1,Qt::Key_M,false},
              {",",1,Qt::Key_Comma,false},{".",1,Qt::Key_Period,false},{"/",1,Qt::Key_Slash,false},
              {"Shift",2.75,Qt::Key_Shift,false} });

    addRow({ {"Ctrl",1.5,Qt::Key_Control,false},{"Win",1.25,Qt::Key_Meta,false},{"Alt",1.25,Qt::Key_Alt,false},
              {"Space",6,Qt::Key_Space,false},{"Alt",1.25,Qt::Key_Alt,false},{"Ctrl",1.5,Qt::Key_Control,false} });

    double mainRows = y;

    // --- Arrow cluster, sits to the right of the main block ---
    double arrowX = mainWidth + 0.75;
    {
        double ay = mainRows - 2.0;
        KeyCell up; up.label = "^"; up.qtKey = Qt::Key_Up;
        up.rect = QRect(int((arrowX+1)*100), int(ay*100), 96, 92);
        cells.push_back(up);
        ay += 1.0;
        KeyCell left; left.label = "<"; left.qtKey = Qt::Key_Left;
        left.rect = QRect(int(arrowX*100), int(ay*100), 96, 92);
        cells.push_back(left);
        KeyCell down; down.label = "v"; down.qtKey = Qt::Key_Down;
        down.rect = QRect(int((arrowX+1)*100), int(ay*100), 96, 92);
        cells.push_back(down);
        KeyCell right; right.label = ">"; right.qtKey = Qt::Key_Right;
        right.rect = QRect(int((arrowX+2)*100), int(ay*100), 96, 92);
        cells.push_back(right);
    }

    // --- Numpad cluster ---
    double npX = arrowX + 3.25;
    {
        double ny = mainRows - 5.0;
        auto addNp = [&](const char* lbl, int key, double col, double row, double w = 1.0, double h = 1.0)
        {
            KeyCell c;
            c.label = QString::fromUtf8(lbl);
            c.qtKey = key | (int)Qt::KeypadModifier;
            c.rect = QRect(int((npX+col)*100), int((ny+row)*100), int(w*100)-4, int(h*92));
            cells.push_back(c);
        };
        addNp("Num", Qt::Key_NumLock, 0, 0);
        addNp("/",   Qt::Key_Slash,   1, 0);
        addNp("*",   Qt::Key_Asterisk,2, 0);
        addNp("7",Qt::Key_7,0,1); addNp("8",Qt::Key_8,1,1); addNp("9",Qt::Key_9,2,1);
        // Standard physical numpad shape: "-" is a single-height key on
        // its own in row 0 (next to Num/  and *), "+" spans rows 1-2,
        // and Enter spans rows 3-4 - previously "-" wrongly spanned
        // rows 0-1 *and* "+" spanned rows 2-3, which put "+" and Enter
        // both in row 3 at the same time, i.e. two keys overlapping on
        // top of each other (the oversized/merged-looking Enter key).
        addNp("-",Qt::Key_Minus,3,0,1,1);
        addNp("4",Qt::Key_4,0,2); addNp("5",Qt::Key_5,1,2); addNp("6",Qt::Key_6,2,2);
        addNp("+",Qt::Key_Plus,3,1,1,2);
        addNp("1",Qt::Key_1,0,3); addNp("2",Qt::Key_2,1,3); addNp("3",Qt::Key_3,2,3);
        addNp("0",Qt::Key_0,0,4,2,1);
        addNp(".",Qt::Key_Period,2,4);
        addNp("Ent",Qt::Key_Enter,3,3,1,2);
    }

    layoutMaxX = 0; layoutMaxY = 0;
    for (const KeyCell& c : cells)
    {
        layoutMaxX = std::max(layoutMaxX, (c.rect.x() + c.rect.width()) / 100.0);
        layoutMaxY = std::max(layoutMaxY, (c.rect.y() + c.rect.height()) / 100.0);
    }
}

void KeyboardPreviewWidget::refreshFromInstance(EmuInstance* inst)
{
    boundKeys.clear();
    boundNumpadKeys.clear();
    if (!inst) { update(); return; }

    // Read from the live mapping actually driving input right now, not
    // from the on-disk config - they can differ (a per-game control scheme
    // override, or an edit made in InputConfigDialog that hasn't been
    // reloaded into this instance yet), and this widget should always show
    // what's really active.
    const int* keyMap = inst->getKeyMapping();
    const int* hkKeyMap = inst->getHotkeyKeyMapping();

    auto record = [&](const QString& label, int stored)
    {
        if (stored == -1) return;
        int base = baseKeyOf(stored);
        if (isNumpadOf(stored)) boundNumpadKeys[base].append(label);
        else                    boundKeys[base].append(label);
    };

    for (int i = 0; i < 12; i++)
    {
        QString label = QString::fromUtf8(EmuInstance::buttonNames[i]) + QStringLiteral(" (DS tuşu)");
        record(label, keyMap[i]);
    }

    auto niceHotkeyLabel = [&](int hk) -> QString
    {
        int idx = 0;
        for (int h : hk_general) { if (h == hk) return QString::fromUtf8(*(hk_general_labels.begin()+idx)); idx++; }
        idx = 0;
        for (int h : hk_addons)  { if (h == hk) return QString::fromUtf8(*(hk_addons_labels.begin()+idx)); idx++; }
        QString raw = QString::fromUtf8(EmuInstance::hotkeyNames[hk]);
        return raw.startsWith("HK_") ? raw.mid(3) : raw;
    };

    for (int hk = 0; hk < HK_MAX; hk++)
    {
        QString label = niceHotkeyLabel(hk) + QStringLiteral(" (Kısayol)");
        record(label, hkKeyMap[hk]);
    }

    update();
}

void KeyboardPreviewWidget::setKeyState(int rawQtKeyWithMods, bool pressed)
{
    int base = baseKeyOf(rawQtKeyWithMods);
    bool numpad = isNumpadOf(rawQtKeyWithMods);
    QSet<int>& set = numpad ? pressedNumpadBase : pressedBase;
    if (pressed) set.insert(base);
    else         set.remove(base);

    // Repaint just this key's cell(s) instead of the whole 400x140 widget
    // on every single press/release during gameplay - cheap enough not to
    // add any noticeable per-keystroke cost.
    if (maxX() <= 0 || maxY() <= 0) { update(); return; }
    double s = std::min(width() / maxX(), height() / maxY());
    QRect dirty;
    for (const KeyCell& c : cells)
    {
        if (baseKeyOf(c.qtKey) != base || isNumpadOf(c.qtKey) != numpad) continue;
        QRect r(int(c.rect.x()/100.0*s), int(c.rect.y()/100.0*s),
                int(c.rect.width()/100.0*s)+2, int(c.rect.height()/100.0*s)+2);
        dirty = dirty.isNull() ? r : dirty.united(r);
    }
    if (!dirty.isNull()) update(dirty.adjusted(-2, -2, 2, 2));
    else update();
}

double KeyboardPreviewWidget::maxX() const { return layoutMaxX; }
double KeyboardPreviewWidget::maxY() const { return layoutMaxY; }

QStringList KeyboardPreviewWidget::controlsForCell(const KeyCell& cell) const
{
    int base = baseKeyOf(cell.qtKey);
    if (isNumpadOf(cell.qtKey)) return boundNumpadKeys.value(base);
    return boundKeys.value(base);
}

const KeyboardPreviewWidget::KeyCell* KeyboardPreviewWidget::cellAt(const QPoint& pos) const
{
    if (cells.empty() || layoutMaxX <= 0 || layoutMaxY <= 0) return nullptr;

    double sx = width() / layoutMaxX;
    double sy = height() / layoutMaxY;
    double s = std::min(sx, sy);

    for (const KeyCell& c : cells)
    {
        QRectF r(c.rect.x()/100.0*s, c.rect.y()/100.0*s, c.rect.width()/100.0*s, c.rect.height()/100.0*s);
        if (r.contains(pos)) return &c;
    }
    return nullptr;
}

bool KeyboardPreviewWidget::event(QEvent* event)
{
    if (event->type() == QEvent::ToolTip)
    {
        QHelpEvent* he = static_cast<QHelpEvent*>(event);
        const KeyCell* c = cellAt(he->pos());
        if (c)
        {
            QStringList controls = controlsForCell(*c);
            if (!controls.isEmpty())
            {
                QToolTip::showText(he->globalPos(), controls.join(", "), this);
                return true;
            }
        }
        QToolTip::hideText();
        return true;
    }
    return QWidget::event(event);
}

void KeyboardPreviewWidget::paintEvent(QPaintEvent*)
{
    if (cells.empty() || layoutMaxX <= 0 || layoutMaxY <= 0) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    double sx = width() / layoutMaxX;
    double sy = height() / layoutMaxY;
    double s = std::min(sx, sy);

    QFont f = p.font();
    f.setPointSizeF(std::max(6.0, s * 0.16));
    p.setFont(f);

    for (const KeyCell& c : cells)
    {
        QRectF r(c.rect.x()/100.0*s, c.rect.y()/100.0*s, c.rect.width()/100.0*s, c.rect.height()/100.0*s);

        bool bound = !controlsForCell(c).isEmpty();
        int base = baseKeyOf(c.qtKey);
        bool pressed = isNumpadOf(c.qtKey) ? pressedNumpadBase.contains(base) : pressedBase.contains(base);

        if (pressed)
        {
            p.setPen(QPen(QColor(255, 225, 120), 1.4));
            p.setBrush(QColor(235, 190, 40, 230));
        }
        else if (bound)
        {
            p.setPen(QPen(QColor(120, 180, 255), 1.2));
            p.setBrush(QColor(60, 120, 220, 200));
        }
        else
        {
            p.setPen(QPen(QColor(255, 255, 255, 40), 1.0));
            p.setBrush(QColor(255, 255, 255, 18));
        }

        p.drawRoundedRect(r, 3, 3);

        p.setPen((pressed || bound) ? QColor(255, 255, 255) : QColor(255, 255, 255, 110));
        p.drawText(r, Qt::AlignCenter, c.label);
    }
}
