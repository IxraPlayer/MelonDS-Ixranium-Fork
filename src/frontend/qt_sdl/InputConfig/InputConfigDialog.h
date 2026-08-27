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

#ifndef INPUTCONFIGDIALOG_H
#define INPUTCONFIGDIALOG_H

#include <QDialog>
#include <QPushButton>
#include <initializer_list>

#include "Config.h"
#include "EmuInstance.h"
#include <QStringList>

static constexpr int keypad_num = 12;

static constexpr std::initializer_list<int> hk_addons =
{
    HK_SolarSensorIncrease,
    HK_SolarSensorDecrease,
    HK_GuitarGripGreen,
    HK_GuitarGripRed,
    HK_GuitarGripYellow,
    HK_GuitarGripBlue,
};

static constexpr std::initializer_list<const char*> hk_addons_labels =
{
    "[Boktai] Sunlight + ",
    "[Boktai] Sunlight - ",
    "[Guitar Grip] Green",
    "[Guitar Grip] Red",
    "[Guitar Grip] Yellow",
    "[Guitar Grip] Blue",
};

static_assert(hk_addons.size() == hk_addons_labels.size());

static constexpr std::initializer_list<int> hk_general =
{
    HK_Pause,
    HK_Reset,
    HK_FrameStep,
    HK_FastForward,
    HK_FastForwardToggle,
    HK_SlowMo,
    HK_SlowMoToggle,
    HK_FrameLimitToggle,
    HK_FullscreenToggle,
    HK_Lid,
    HK_Mic,
    HK_SwapScreens,
    HK_SwapScreenEmphasis,
    HK_PowerButton,
    HK_VolumeUp,
    HK_VolumeDown,
    HK_AudioMuteToggle
};

static constexpr std::initializer_list<const char*> hk_general_labels =
{
    "Pause/resume",
    "Reset",
    "Frame step",
    "Fast forward",
    "Toggle fast forward",
    "Slow mo",
    "Toggle slow mo",
    "Toggle FPS limit",
    "Toggle fullscreen",
    "Close/open lid",
    "Microphone",
    "Swap screens",
    "Swap screen emphasis",
    "DSi Power button",
    "DSi Volume up",
    "DSi Volume down",
    "Toggle audio mute"
};

static_assert(hk_general.size() == hk_general_labels.size());

// Full-keypad control scheme presets.
// Field order MUST match dskeylabels/keypadKeyMap order:
// A, B, X, Y, Left, Right, Up, Down, L, R, Select, Start.
struct ControlPreset
{
    const char* name;
    int a, b, x, y, left, right, up, down, l, r, select, start;
};

#define NUMPAD(k) (Qt::Key_##k | Qt::KeypadModifier)

static constexpr ControlPreset control_presets[] =
{
    // Classic melonDS-style layout: arrow keys + X/Z/A/S cluster.
    { "Orijinal (Ok Tuşları)",
      /*A*/ Qt::Key_X, /*B*/ Qt::Key_Z, /*X*/ Qt::Key_S, /*Y*/ Qt::Key_A,
      /*Left*/ Qt::Key_Left, /*Right*/ Qt::Key_Right, /*Up*/ Qt::Key_Up, /*Down*/ Qt::Key_Down,
      /*L*/ Qt::Key_Q, /*R*/ Qt::Key_W, /*Select*/ Qt::Key_Backspace, /*Start*/ Qt::Key_Return },

    // WASD movement with a right-hand action cluster, matching most PC games.
    { "WASD",
      /*A*/ Qt::Key_Space, /*B*/ Qt::Key_Shift, /*X*/ Qt::Key_K, /*Y*/ Qt::Key_J,
      /*Left*/ Qt::Key_A, /*Right*/ Qt::Key_D, /*Up*/ Qt::Key_W, /*Down*/ Qt::Key_S,
      /*L*/ Qt::Key_Q, /*R*/ Qt::Key_E, /*Select*/ Qt::Key_Backspace, /*Start*/ Qt::Key_Return },

    // Numpad cluster: direction on 8/4/6/2, actions on the surrounding keys.
    // OR'd with Qt::KeypadModifier so this binds the *numpad* keys specifically,
    // not the digit row (matches how EmuInstanceInput.cpp/KeyMapButton store captured keys).
    { "Numpad",
      /*A*/ NUMPAD(1), /*B*/ NUMPAD(3), /*X*/ NUMPAD(9), /*Y*/ NUMPAD(7),
      /*Left*/ NUMPAD(4), /*Right*/ NUMPAD(6), /*Up*/ NUMPAD(8), /*Down*/ NUMPAD(2),
      /*L*/ NUMPAD(0), /*R*/ Qt::Key_Enter | Qt::KeypadModifier,
      /*Select*/ NUMPAD(Period), /*Start*/ NUMPAD(5) },
};

#undef NUMPAD
static constexpr int control_presets_custom_index = -1;

class KeyMapButton;

namespace Ui { class InputConfigDialog; }
class InputConfigDialog;
class MainWindow;

class InputConfigDialog : public QDialog
{
    Q_OBJECT

signals:
    // Fired every time a mapping actually changes and gets committed (see
    // commitAndSave) - lets whatever's showing a live keyboard preview
    // (the docked overlay, the pause menu) stay in sync without needing
    // this dialog to be closed and reopened first.
    void mappingsChanged();

public:
    explicit InputConfigDialog(QWidget* parent);
    ~InputConfigDialog();

    SDL_Joystick* getJoystick();
    std::shared_ptr<SDL_mutex> getJoyMutex();

    static InputConfigDialog* currentDlg;
    static InputConfigDialog* openDlg(QWidget* parent)
    {
        if (currentDlg)
        {
            currentDlg->activateWindow();
            return currentDlg;
        }

        currentDlg = new InputConfigDialog(parent);
        currentDlg->open();
        return currentDlg;
    }
    static void closeDlg()
    {
        currentDlg = nullptr;
    }

private slots:
    void on_InputConfigDialog_accepted();
    void on_InputConfigDialog_rejected();

    void on_btnKeyMapSwitch_clicked();
    void on_btnJoyMapSwitch_clicked();
    void on_cbxJoystick_currentIndexChanged(int id);
    void on_cbxControlPreset_currentIndexChanged(int idx);
    void commitAndSave();
    void on_btnSaveScheme_clicked();
    void on_btnDeleteScheme_clicked();

private:
    void populatePage(QWidget* page,
        const std::initializer_list<const char*>& labels,
        int* keymap, int* joymap);
    void setupKeypadPage();
    void setupControlPresets();
    int detectControlPreset();
    // Rebuilds the combo box contents (built-ins + saved schemes + "Özel")
    // and selects whichever entry matches the current keypadKeyMap, or a
    // specific saved scheme name if preferSelectName is non-empty (used
    // right after saving/deleting one, since the mapping itself doesn't
    // change but the row we want selected does).
    void refreshPresetCombo(const QString& preferSelectName = QString());

    Ui::InputConfigDialog* ui;

    MainWindow* mainWindow;
    EmuInstance* emuInstance;

    // Keypad key-mapping buttons (A,B,X,Y,Left,Right,Up,Down,L,R,Select,Start —
    // same order as keypadKeyMap), kept around so a preset switch can refresh
    // their labels immediately.
    KeyMapButton* keypadKeyButtons[keypad_num] = {};
    bool applyingPreset = false;

    // Names of user-saved schemes, in the same order they appear in the
    // combo box right after the built-in presets (see refreshPresetCombo).
    QStringList customSchemeNames;

    int keypadKeyMap[12], keypadJoyMap[12];
    int addonsKeyMap[hk_addons.size()], addonsJoyMap[hk_addons.size()];
    int hkGeneralKeyMap[hk_general.size()], hkGeneralJoyMap[hk_general.size()];
    int joystickID;
};


#endif // INPUTCONFIGDIALOG_H
