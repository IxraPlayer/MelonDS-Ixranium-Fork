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
#include <QLineEdit>
#include <QFileDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QStandardPaths>
#include <functional>

#include "types.h"
#include "Config.h"
#include "main.h"
#include "GPU3D_Texcache.h" // melonDS::DumpSpriteAtlasRequested

#include "AtlasSettingsDialog.h"
#include "EmuInstance.h"

namespace
{
    // Same minimal keyboard-only capture button as DebugSettingsDialog's
    // DebugHotkeyButton - kept as its own copy rather than sharing, for
    // the same reason (avoids dragging in InputConfigDialog.h for a
    // joystick binding path this page doesn't need).
    class AtlasHotkeyButton : public QPushButton
    {
    public:
        AtlasHotkeyButton(int* mapping, std::function<void()> onChanged)
            : QPushButton(), mapping(mapping), onChanged(onChanged)
        {
            setCheckable(true);
            setText(mappingText());
            setFocusPolicy(Qt::StrongFocus);
            connect(this, &QPushButton::clicked, this, &AtlasHotkeyButton::onClick);
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

AtlasSettingsDialog::AtlasSettingsDialog(QWidget* parent) : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Atlas settings");

    mainWindow = (MainWindow*)parent;
    emuInstance = mainWindow->getEmuInstance();

    Config::Table& instcfg = emuInstance->getLocalConfig();
    Config::Table keycfg = instcfg.GetTable("Keyboard");
    hkKeyMapping = keycfg.GetInt(EmuInstance::hotkeyNames[HK_DumpSpriteAtlas]);

    auto* layout = new QVBoxLayout(this);

    auto* group = new QGroupBox(tr("Sprite atlas dump"));
    auto* groupLayout = new QVBoxLayout(group);

    auto* hint = new QLabel(tr(
        "Saves the currently running game's sprite atlas (the exact "
        "texture GPU2D samples sprites from that frame) as a PNG in "
        "your Pictures folder - one file per screen/engine. Useful for "
        "reporting sprite rendering glitches: it shows whether the bad "
        "pixels are already in the atlas or only appear once drawn to "
        "screen."));
    hint->setWordWrap(true);
    groupLayout->addWidget(hint);

    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel(tr("Dump atlas hotkey")));
    row->addStretch();
    auto* keyBtn = new AtlasHotkeyButton(&hkKeyMapping, [this]()
    {
        Config::Table& cfg = emuInstance->getLocalConfig();
        Config::Table kcfg = cfg.GetTable("Keyboard");
        kcfg.SetInt(EmuInstance::hotkeyNames[HK_DumpSpriteAtlas], hkKeyMapping);
        emuInstance->inputLoadConfig();
    });
    row->addWidget(keyBtn);
    groupLayout->addLayout(row);

    auto* pathRow = new QHBoxLayout();
    pathRow->addWidget(new QLabel(tr("Save to")));

    Config::Table& globalCfg = emuInstance->getGlobalConfig();
    QString savedPath = globalCfg.GetQString("AtlasDumpPath");
    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    auto* pathEdit = new QLineEdit(savedPath.isEmpty() ? defaultPath : savedPath);
    pathRow->addWidget(pathEdit, 1);

    auto* browseBtn = new QPushButton(tr("Browse..."));
    connect(browseBtn, &QPushButton::clicked, this, [this, pathEdit]()
    {
        QString dir = QFileDialog::getExistingDirectory(this,
            tr("Select atlas dump folder..."), pathEdit->text());
        if (dir.isEmpty()) return;
        pathEdit->setText(dir);
        emuInstance->getGlobalConfig().SetQString("AtlasDumpPath", dir);
    });
    pathRow->addWidget(browseBtn);
    groupLayout->addLayout(pathRow);

    // Also save on manual edit (e.g. pasting a path), not just Browse.
    connect(pathEdit, &QLineEdit::editingFinished, this, [this, pathEdit]()
    {
        emuInstance->getGlobalConfig().SetQString("AtlasDumpPath", pathEdit->text());
    });

    auto* dumpNowBtn = new QPushButton(tr("Dump now"));
    connect(dumpNowBtn, &QPushButton::clicked, this, []()
    {
        melonDS::DumpSpriteAtlasRequested.store(true, std::memory_order_relaxed);
    });
    groupLayout->addWidget(dumpNowBtn);

    layout->addWidget(group);
    layout->addStretch();
}

AtlasSettingsDialog::~AtlasSettingsDialog()
{
}

void AtlasSettingsDialog::done(int r)
{
    // Save happens immediately on key capture / button click above, same
    // reasoning as DebugSettingsDialog::done() - this is a no-op safety net.
    QDialog::done(r);
}
