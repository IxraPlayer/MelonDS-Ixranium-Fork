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

#ifndef ATLASSETTINGSDIALOG_H
#define ATLASSETTINGSDIALOG_H

#include <QDialog>

class MainWindow;
class EmuInstance;

// Settings > Atlas. Lets the user bind a key to dump the currently
// running game's sprite atlas (whatever GPU2D_OpenGL is sampling from
// that frame - the Ixranium-upscaled one if that's on, the native one
// otherwise) to a PNG in the Pictures folder, for sending along when
// reporting a sprite rendering bug. Also has a "Dump now" button that
// doesn't need the key bound at all. Same pattern as
// DebugSettingsDialog (self-contained keyboard-only bind button,
// no OK/Cancel box - saved immediately on capture).
class AtlasSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AtlasSettingsDialog(QWidget* parent);
    ~AtlasSettingsDialog();

private slots:
    void done(int r) override;

private:
    MainWindow* mainWindow;
    EmuInstance* emuInstance;

    int hkKeyMapping;
};

#endif // ATLASSETTINGSDIALOG_H
