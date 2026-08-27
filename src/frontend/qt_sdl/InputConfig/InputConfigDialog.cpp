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

#include <QGroupBox>
#include <QLabel>
#include <QKeyEvent>
#include <QDebug>
#include <QComboBox>
#include <iterator>

#include <SDL2/SDL.h>

#include "types.h"
#include "Platform.h"

#include "InputConfigDialog.h"
#include "ui_InputConfigDialog.h"
#include "MapButton.h"
#include "ControlSchemeStore.h"

#include <QInputDialog>
#include <QMessageBox>


using namespace melonDS;
InputConfigDialog* InputConfigDialog::currentDlg = nullptr;

const int dskeyorder[12] = {0, 1, 10, 11, 5, 4, 6, 7, 9, 8, 2, 3};
const char* dskeylabels[12] = {"A", "B", "X", "Y", "Left", "Right", "Up", "Down", "L", "R", "Select", "Start"};

InputConfigDialog::InputConfigDialog(QWidget* parent) : QDialog(parent), ui(new Ui::InputConfigDialog)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);

    mainWindow = (MainWindow*)parent;
    emuInstance = mainWindow->getEmuInstance();

    Config::Table& instcfg = emuInstance->getLocalConfig();
    Config::Table keycfg = instcfg.GetTable("Keyboard");
    Config::Table joycfg = instcfg.GetTable("Joystick");

    for (int i = 0; i < keypad_num; i++)
    {
        const char* btn = EmuInstance::buttonNames[dskeyorder[i]];
        keypadKeyMap[i] = keycfg.GetInt(btn);
        keypadJoyMap[i] = joycfg.GetInt(btn);
    }

    int i = 0;
    for (int hotkey : hk_addons)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        addonsKeyMap[i] = keycfg.GetInt(btn);
        addonsJoyMap[i] = joycfg.GetInt(btn);
        i++;
    }

    i = 0;
    for (int hotkey : hk_general)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        hkGeneralKeyMap[i] = keycfg.GetInt(btn);
        hkGeneralJoyMap[i] = joycfg.GetInt(btn);
        i++;
    }

    populatePage(ui->tabAddons, hk_addons_labels, addonsKeyMap, addonsJoyMap);
    populatePage(ui->tabHotkeysGeneral, hk_general_labels, hkGeneralKeyMap, hkGeneralJoyMap);

    joystickID = instcfg.GetInt("JoystickID");

    int njoy = SDL_NumJoysticks();
    if (njoy > 0)
    {
        for (int i = 0; i < njoy; i++)
        {
            const char* name = SDL_JoystickNameForIndex(i);
            ui->cbxJoystick->addItem(QString(name));
        }
        ui->cbxJoystick->setCurrentIndex(joystickID);
    }
    else
    {
        ui->cbxJoystick->addItem("(no joysticks available)");
        ui->cbxJoystick->setEnabled(false);
    }

    setupKeypadPage();
    setupControlPresets();

    int inst = emuInstance->getInstanceID();
    if (inst > 0)
        ui->lblInstanceNum->setText(QString("Configuring mappings for instance %1").arg(inst+1));
    else
        ui->lblInstanceNum->hide();
}

InputConfigDialog::~InputConfigDialog()
{
    delete ui;
}

void InputConfigDialog::setupKeypadPage()
{
    for (int i = 0; i < keypad_num; i++)
    {
        QPushButton* pushButtonKey = this->findChild<QPushButton*>(QStringLiteral("btnKey") + dskeylabels[i]);
        QPushButton* pushButtonJoy = this->findChild<QPushButton*>(QStringLiteral("btnJoy") + dskeylabels[i]);

        KeyMapButton* keyMapButtonKey = new KeyMapButton(&keypadKeyMap[i], false);
        JoyMapButton* keyMapButtonJoy = new JoyMapButton(&keypadJoyMap[i], false);

        pushButtonKey->parentWidget()->layout()->replaceWidget(pushButtonKey, keyMapButtonKey);
        pushButtonJoy->parentWidget()->layout()->replaceWidget(pushButtonJoy, keyMapButtonJoy);

        delete pushButtonKey;
        delete pushButtonJoy;

        // dskeylabels[i] order (A,B,X,Y,Left,Right,Up,Down,L,R,Select,Start) matches
        // keypadKeyMap[i], so we can just keep the button by index for control presets.
        keypadKeyButtons[i] = keyMapButtonKey;

        connect(keyMapButtonKey, &KeyMapButton::mappingChanged, this, &InputConfigDialog::commitAndSave);
        connect(keyMapButtonJoy, &JoyMapButton::mappingChanged, this, &InputConfigDialog::commitAndSave);

        if (ui->cbxJoystick->isEnabled())
        {
            ui->stackMapping->setCurrentIndex(1);
        }
    }
}

void InputConfigDialog::setupControlPresets()
{
    connect(ui->cbxControlPreset, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &InputConfigDialog::on_cbxControlPreset_currentIndexChanged);

    refreshPresetCombo();
}

// Order must match ControlPreset's fields (A,B,X,Y,Left,Right,Up,Down,L,R,Select,Start),
// which is also keypadKeyMap's order.
static int presetValueAt(const ControlPreset& p, int i)
{
    const int vals[keypad_num] = { p.a, p.b, p.x, p.y, p.left, p.right,
                                    p.up, p.down, p.l, p.r, p.select, p.start };
    return vals[i];
}

// keypadKeyMap is in display order (dskeylabels: A,B,X,Y,Left,Right,Up,Down,
// L,R,Select,Start). Saved schemes are stored in EmuInstance::buttonNames[]
// native order instead, so they can be applied straight to a running
// instance's keyMapping[] without needing this dialog's ordering at all
// (see EmuInstance::applyKeypadKeyOverride and MainWindow::onLibraryGameActivated).
static void keypadMapToNativeOrder(const int (&display)[keypad_num], int (&native)[12])
{
    for (int k = 0; k < keypad_num; k++)
        native[dskeyorder[k]] = display[k];
}

static void nativeOrderToKeypadMap(const int (&native)[12], int (&display)[keypad_num])
{
    for (int k = 0; k < keypad_num; k++)
        display[k] = native[dskeyorder[k]];
}

int InputConfigDialog::detectControlPreset()
{
    for (int i = 0; i < (int)std::size(control_presets); i++)
    {
        const ControlPreset& p = control_presets[i];
        bool match = true;
        for (int k = 0; k < keypad_num; k++)
        {
            if (keypadKeyMap[k] != presetValueAt(p, k)) { match = false; break; }
        }
        if (match) return i;
    }

    int nativeCur[12];
    keypadMapToNativeOrder(keypadKeyMap, nativeCur);
    for (int i = 0; i < customSchemeNames.size(); i++)
    {
        int nativeSaved[12];
        if (!ControlSchemeStore::load(customSchemeNames[i], nativeSaved)) continue;
        bool match = true;
        for (int k = 0; k < 12; k++)
        {
            if (nativeCur[k] != nativeSaved[k]) { match = false; break; }
        }
        if (match) return (int)std::size(control_presets) + i;
    }

    return control_presets_custom_index;
}

void InputConfigDialog::refreshPresetCombo(const QString& preferSelectName)
{
    customSchemeNames = ControlSchemeStore::listNames();

    applyingPreset = true;
    ui->cbxControlPreset->clear();
    for (const ControlPreset& p : control_presets)
        ui->cbxControlPreset->addItem(QString::fromUtf8(p.name));
    for (const QString& name : customSchemeNames)
        ui->cbxControlPreset->addItem(name);
    ui->cbxControlPreset->addItem("Özel");

    int builtinCount = (int)std::size(control_presets);
    int selectIdx;
    if (!preferSelectName.isEmpty() && customSchemeNames.contains(preferSelectName))
        selectIdx = builtinCount + customSchemeNames.indexOf(preferSelectName);
    else
    {
        int detected = detectControlPreset();
        selectIdx = (detected == control_presets_custom_index)
            ? builtinCount + customSchemeNames.size()
            : detected;
    }
    ui->cbxControlPreset->setCurrentIndex(selectIdx);
    applyingPreset = false;

    ui->btnDeleteScheme->setEnabled(selectIdx >= builtinCount &&
                                     selectIdx < builtinCount + customSchemeNames.size());
}

void InputConfigDialog::on_cbxControlPreset_currentIndexChanged(int idx)
{
    if (applyingPreset) return;

    int builtinCount = (int)std::size(control_presets);
    ui->btnDeleteScheme->setEnabled(idx >= builtinCount && idx < builtinCount + customSchemeNames.size());

    if (idx < 0) return;

    if (idx < builtinCount)
    {
        const ControlPreset& p = control_presets[idx];
        for (int k = 0; k < keypad_num; k++)
            keypadKeyMap[k] = presetValueAt(p, k);
    }
    else if (idx < builtinCount + customSchemeNames.size())
    {
        int native[12];
        if (!ControlSchemeStore::load(customSchemeNames[idx - builtinCount], native))
            return;
        nativeOrderToKeypadMap(native, keypadKeyMap);
    }
    else
    {
        return; // "Özel" selected, nothing to apply
    }

    for (KeyMapButton* btn : keypadKeyButtons)
        if (btn) btn->refresh();

    // Picking a preset/scheme is a decisive action on its own; commit it
    // straight through rather than waiting for OK.
    commitAndSave();
}

void InputConfigDialog::on_btnSaveScheme_clicked()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, "Kontrol Şeması Kaydet", "Şema adı:",
                                          QLineEdit::Normal, QString(), &ok);
    name = name.trimmed();
    if (!ok || name.isEmpty()) return;

    if (customSchemeNames.contains(name))
    {
        if (QMessageBox::question(this, "Kontrol Şeması Kaydet",
                QString("\"%1\" adlı şema zaten var. Üzerine yazılsın mı?").arg(name))
            != QMessageBox::Yes)
            return;
    }

    int native[12];
    keypadMapToNativeOrder(keypadKeyMap, native);
    ControlSchemeStore::save(name, native);

    refreshPresetCombo(name);
}

void InputConfigDialog::on_btnDeleteScheme_clicked()
{
    int idx = ui->cbxControlPreset->currentIndex();
    int builtinCount = (int)std::size(control_presets);
    if (idx < builtinCount || idx >= builtinCount + customSchemeNames.size())
        return; // a built-in preset or "Özel" is selected, nothing to delete

    QString name = customSchemeNames[idx - builtinCount];
    if (QMessageBox::question(this, "Kontrol Şeması Sil",
            QString("\"%1\" adlı şema silinsin mi?").arg(name)) != QMessageBox::Yes)
        return;

    ControlSchemeStore::remove(name);
    refreshPresetCombo();
}
// Writes every mapping array (keypad, addon hotkeys, general hotkeys, both
// keyboard and joystick sides, plus the selected joystick device) straight
// through to config and saves immediately. Called after every individual
// mapping change (button capture, clear, preset pick) rather than only on
// OK, so nothing is lost if the dialog is closed via Cancel/Esc/X - those
// used to discard any manual remap that wasn't a full preset pick.
void InputConfigDialog::commitAndSave()
{
    Config::Table& instcfg = emuInstance->getLocalConfig();
    Config::Table keycfg = instcfg.GetTable("Keyboard");
    Config::Table joycfg = instcfg.GetTable("Joystick");

    for (int i = 0; i < keypad_num; i++)
    {
        const char* btn = EmuInstance::buttonNames[dskeyorder[i]];
        keycfg.SetInt(btn, keypadKeyMap[i]);
        joycfg.SetInt(btn, keypadJoyMap[i]);
    }

    int i = 0;
    for (int hotkey : hk_addons)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        keycfg.SetInt(btn, addonsKeyMap[i]);
        joycfg.SetInt(btn, addonsJoyMap[i]);
        i++;
    }

    i = 0;
    for (int hotkey : hk_general)
    {
        const char* btn = EmuInstance::hotkeyNames[hotkey];
        keycfg.SetInt(btn, hkGeneralKeyMap[i]);
        joycfg.SetInt(btn, hkGeneralJoyMap[i]);
        i++;
    }

    instcfg.SetInt("JoystickID", joystickID);
    Config::Save();

    emuInstance->inputLoadConfig();
    emit mappingsChanged();
}

void InputConfigDialog::populatePage(QWidget* page,
    const std::initializer_list<const char*>& labels,
    int* keymap, int* joymap)
{
    // kind of a hack
    bool ishotkey = (page != ui->tabInput);

    QHBoxLayout* main_layout = new QHBoxLayout();

    QGroupBox* group;
    QGridLayout* group_layout;

    group = new QGroupBox("Keyboard mappings:");
    main_layout->addWidget(group);
    group_layout = new QGridLayout();
    group_layout->setSpacing(1);
    int i = 0;
    for (const char* labelStr : labels)
    {
        QLabel* label = new QLabel(QString(labelStr)+":");
        KeyMapButton* btn = new KeyMapButton(&keymap[i], ishotkey);
        connect(btn, &KeyMapButton::mappingChanged, this, &InputConfigDialog::commitAndSave);

        group_layout->addWidget(label, i, 0);
        group_layout->addWidget(btn, i, 1);
        i++;
    }
    group_layout->setRowStretch(labels.size(), 1);
    group->setLayout(group_layout);
    group->setMinimumWidth(275);

    group = new QGroupBox("Joystick mappings:");
    main_layout->addWidget(group);
    group_layout = new QGridLayout();
    group_layout->setSpacing(1);
    i = 0;
    for (const char* labelStr : labels)
    {
        QLabel* label = new QLabel(QString(labelStr)+":");
        JoyMapButton* btn = new JoyMapButton(&joymap[i], ishotkey);
        connect(btn, &JoyMapButton::mappingChanged, this, &InputConfigDialog::commitAndSave);

        group_layout->addWidget(label, i, 0);
        group_layout->addWidget(btn, i, 1);
        i++;
    }
    group_layout->setRowStretch(labels.size(), 1);
    group->setLayout(group_layout);
    group->setMinimumWidth(275);

    page->setLayout(main_layout);
}

void InputConfigDialog::on_InputConfigDialog_accepted()
{
    // Every mapping change already committed itself the moment it happened
    // (see commitAndSave), so OK just needs to persist the joystick device
    // choice and close - one final commit covers that.
    commitAndSave();
    closeDlg();
}

void InputConfigDialog::on_InputConfigDialog_rejected()
{
    // All mapping edits made during this dialog session were already saved
    // live as they happened, so Cancel/Esc/X only restores the joystick
    // device selection rather than discarding remaps like it used to.
    Config::Table& instcfg = emuInstance->getLocalConfig();
    emuInstance->setJoystick(instcfg.GetInt("JoystickID"));

    closeDlg();
}

void InputConfigDialog::on_btnKeyMapSwitch_clicked()
{
    ui->stackMapping->setCurrentIndex(0);
}

void InputConfigDialog::on_btnJoyMapSwitch_clicked()
{
    ui->stackMapping->setCurrentIndex(1);
}

void InputConfigDialog::on_cbxJoystick_currentIndexChanged(int id)
{
    // prevent a spurious change
    if (ui->cbxJoystick->count() < 2) return;

    joystickID = id;
    emuInstance->setJoystick(id);
}

SDL_Joystick* InputConfigDialog::getJoystick()
{
    return emuInstance->getJoystick();
}

std::shared_ptr<SDL_mutex> InputConfigDialog::getJoyMutex()
{
    return emuInstance->getJoyMutex();
}
