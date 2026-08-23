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

#ifndef DEBUGSETTINGSDIALOG_H
#define DEBUGSETTINGSDIALOG_H

#include <QDialog>

class MainWindow;
class EmuInstance;

// Settings > Debug settings. Lets the user bind a key to toggle the
// in-game debug overlay (HK_ToggleDebugOverlay), and pick which stats
// that overlay actually shows via one checkbox per field - see
// DebugOverlayFields.h for the field list, and ScreenPanel (Screen.h/.cpp)
// for how the overlay itself is drawn.
class DebugSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit DebugSettingsDialog(QWidget* parent);
    ~DebugSettingsDialog();

private slots:
    void done(int r) override;

private:
    MainWindow* mainWindow;
    EmuInstance* emuInstance;

    int hkKeyMapping;
};

#endif // DEBUGSETTINGSDIALOG_H
