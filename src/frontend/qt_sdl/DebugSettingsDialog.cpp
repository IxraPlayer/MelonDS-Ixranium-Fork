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

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QGroupBox>
#include <QPushButton>
#include <QCheckBox>
#include <QKeyEvent>
#include <QKeySequence>
#include <functional>

#include "types.h"
#include "Config.h"
#include "main.h"

#include "DebugSettingsDialog.h"
#include "EmuInstance.h"
#include "DebugOverlayFields.h"

namespace
{
    // Deliberately NOT reusing InputConfig/MapButton.h's KeyMapButton here:
    // that header also defines JoyMapButton, whose methods call into a full
    // InputConfigDialog (qobject_cast<InputConfigDialog*>, getJoystick(),
    // getJoyMutex()) which isn't available/needed on this dialog and would
    // otherwise force us to drag in InputConfigDialog.h just to satisfy the
    // compiler. This is a minimal, self-contained keyboard-only capture
    // button - no joystick binding, since the debug overlay toggle is
    // keyboard-only by design.
    class DebugHotkeyButton : public QPushButton
    {
    public:
        // onChanged is invoked right after a key (or Backspace-clear) is
        // captured, so the binding is persisted immediately - this page
        // has no OK/Cancel button box, so QDialog::done() never fires and
        // can't be relied on to save.
        DebugHotkeyButton(int* mapping, std::function<void()> onChanged)
            : QPushButton(), mapping(mapping), onChanged(onChanged)
        {
            setCheckable(true);
            setText(mappingText());
            setFocusPolicy(Qt::StrongFocus);
            connect(this, &QPushButton::clicked, this, &DebugHotkeyButton::onClick);
        }

    protected:
        void keyPressEvent(QKeyEvent* event) override
        {
            if (!isChecked()) { QPushButton::keyPressEvent(event); return; }

            int key = event->key();
            int mod = event->modifiers();
            bool ismod = (key == Qt::Key_Control || key == Qt::Key_Alt ||
                          key == Qt::Key_AltGr || key == Qt::Key_Shift ||
                          key == Qt::Key_Meta);

            if (!mod)
            {
                if (key == Qt::Key_Escape) { click(); return; }
                if (key == Qt::Key_Backspace) { *mapping = -1; click(); if (onChanged) onChanged(); return; }
            }

            // Hotkeys ignore bare modifier presses, same as the regular
            // hotkey mapping buttons - you bind e.g. Ctrl+D, not Ctrl alone.
            if (ismod)
                return;

            *mapping = key | mod;
            click();
            if (onChanged) onChanged();
        }

        void focusOutEvent(QFocusEvent* event) override
        {
            if (isChecked())
                click();
            QPushButton::focusOutEvent(event);
        }

        bool focusNextPrevChild(bool) override { return false; }

    private:
        void onClick()
        {
            setText(isChecked() ? "[press key]" : mappingText());
        }

        QString mappingText() const
        {
            int key = *mapping;
            if (key == -1) return "None";

            switch (key)
            {
            case Qt::Key_Control: return "Ctrl";
            case Qt::Key_Alt:     return "Alt";
            case Qt::Key_AltGr:   return "AltGr";
            case Qt::Key_Shift:   return "Shift";
            case Qt::Key_Meta:    return "Meta";
            }

            QKeySequence seq(key);
            return seq.toString(QKeySequence::NativeText).replace("&", "&&");
        }

        int* mapping;
        std::function<void()> onChanged;
    };
}

DebugSettingsDialog::DebugSettingsDialog(QWidget* parent) : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Debug settings");

    mainWindow = (MainWindow*)parent;
    emuInstance = mainWindow->getEmuInstance();

    Config::Table& instcfg = emuInstance->getLocalConfig();
    Config::Table keycfg = instcfg.GetTable("Keyboard");
    hkKeyMapping = keycfg.GetInt(EmuInstance::hotkeyNames[HK_ToggleDebugOverlay]);

    auto* layout = new QVBoxLayout(this);

    auto* group = new QGroupBox(tr("Debug overlay"));
    auto* groupLayout = new QVBoxLayout(group);

    auto* hint = new QLabel(tr(
        "Assign a key to show/hide the in-game FPS, CPU and RAM overlay."));
    hint->setWordWrap(true);
    groupLayout->addWidget(hint);

    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("Toggle debug overlay")));
    row->addStretch();
    auto* keyBtn = new DebugHotkeyButton(&hkKeyMapping, [this]()
    {
        // Persist + apply immediately - see DebugHotkeyButton comment above
        // for why we can't wait for done().
        Config::Table& cfg = emuInstance->getLocalConfig();
        Config::Table kcfg = cfg.GetTable("Keyboard");
        kcfg.SetInt(EmuInstance::hotkeyNames[HK_ToggleDebugOverlay], hkKeyMapping);
        emuInstance->inputLoadConfig();
    });
    row->addWidget(keyBtn);
    groupLayout->addLayout(row);

    layout->addWidget(group);

    // One checkbox per overlay line (see DebugOverlayFields.h). Ticking a
    // box adds that field to the on-screen overlay, unticking removes it -
    // saved immediately to the global config as a bitmask so it's shared
    // across instances/windows the same way the panel theme is, and read
    // back by ScreenPanel::updateDebugOverlayText() every refresh.
    auto* fieldsGroup = new QGroupBox(tr("Overlay fields"));
    auto* fieldsLayout = new QVBoxLayout(fieldsGroup);

    auto* fieldsHint = new QLabel(tr("Choose what the in-game debug overlay shows."));
    fieldsHint->setWordWrap(true);
    fieldsLayout->addWidget(fieldsHint);

    Config::Table& globalCfg = emuInstance->getGlobalConfig();
    unsigned int fieldMask = (unsigned int)globalCfg.GetInt("DebugOverlay.Fields");

    for (int i = 0; i < DBGOV_COUNT; i++)
    {
        const DebugOverlayFieldInfo& info = kDebugOverlayFields[i];
        auto* cb = new QCheckBox(tr(info.label));
        cb->setChecked(fieldMask & (1u << info.id));

        connect(cb, &QCheckBox::toggled, this, [this, id = info.id](bool checked)
        {
            Config::Table& cfg = emuInstance->getGlobalConfig();
            unsigned int mask = (unsigned int)cfg.GetInt("DebugOverlay.Fields");
            if (checked)
                mask |= (1u << id);
            else
                mask &= ~(1u << id);
            cfg.SetInt("DebugOverlay.Fields", (int)mask);
        });

        fieldsLayout->addWidget(cb);
    }

    layout->addWidget(fieldsGroup);
    layout->addStretch();
}

DebugSettingsDialog::~DebugSettingsDialog()
{
}

void DebugSettingsDialog::done(int r)
{
    // Kept as a harmless no-op safety net - the actual save now happens
    // immediately when the key is captured (see DebugHotkeyButton's
    // onChanged callback above), since this embedded page has no
    // OK/Cancel button box and done() is never invoked in practice.
    QDialog::done(r);
}
