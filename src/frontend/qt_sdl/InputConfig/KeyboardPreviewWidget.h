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

#ifndef KEYBOARDPREVIEWWIDGET_H
#define KEYBOARDPREVIEWWIDGET_H

#include <QWidget>
#include <QMap>
#include <QSet>
#include <QString>
#include <vector>

class EmuInstance;

// Small read-only "which keys are mapped" preview: draws a full keyboard
// (main block + arrows + numpad) and colors every key that currently has
// a DS button or hotkey bound to it. Hovering a bound (blue) key shows a
// tooltip with the name(s) of what it's bound to; everything else stays
// grey/"off".
class KeyboardPreviewWidget : public QWidget
{
    Q_OBJECT

public:
    explicit KeyboardPreviewWidget(QWidget* parent = nullptr);

    // Re-reads the keyboard mapping from the given instance's config and
    // repaints. Call this whenever the mapping may have changed (e.g. on
    // pause-menu open) since this widget doesn't listen for changes itself.
    void refreshFromInstance(EmuInstance* inst);

    // Marks a physical key as currently held down (or released) so it can
    // glow yellow in real time. rawQtKeyWithMods should be event->key()
    // OR'd with Qt::KeypadModifier when it came from the numpad, so the
    // numpad cluster and the digit row light up independently.
    void setKeyState(int rawQtKeyWithMods, bool pressed);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* event) override;

private:
    struct KeyCell
    {
        QRect rect;
        QString label;
        int qtKey; // base Qt::Key value (no modifier bits), or -1 for spacer
    };

    void buildLayout();
    const KeyCell* cellAt(const QPoint& pos) const;
    double maxX() const;
    double maxY() const;
    // Returns the display names of everything bound to this cell's key
    // (matching ignores the KeypadModifier/right-side bit so both a plain
    // key and its numpad/right-hand twin can be told apart visually).
    QStringList controlsForCell(const KeyCell& cell) const;

    std::vector<KeyCell> cells;
    // Overall bounding box of `cells` in the same *100 unscaled space,
    // computed once in buildLayout() instead of re-scanned on every single
    // paint/hit-test/hover-poll (the layout never changes after construction).
    double layoutMaxX = 0, layoutMaxY = 0;
    // base Qt::Key (masked) -> list of "control name" bound to it.
    // Keys are stored twice when needed: once as-is and once with the
    // Keypad flag stripped, so numpad-cluster presets still light up the
    // numpad block specifically rather than the digit row.
    QMap<int, QStringList> boundKeys;
    QMap<int, QStringList> boundNumpadKeys;

    // currently physically held-down keys, for the yellow "live" glow
    QSet<int> pressedBase;
    QSet<int> pressedNumpadBase;
};

#endif // KEYBOARDPREVIEWWIDGET_H
