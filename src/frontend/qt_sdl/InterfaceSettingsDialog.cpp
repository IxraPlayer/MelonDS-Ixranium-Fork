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

#include <QStyleFactory>
#include <QFile>
#include <QTextStream>
#include "InterfaceSettingsDialog.h"
#include "ui_InterfaceSettingsDialog.h"
#include "LibraryScreen.h"

#include "types.h"
#include "Platform.h"
#include "Config.h"
#include "main.h"
#include "PopupCornerFix.h"

InterfaceSettingsDialog* InterfaceSettingsDialog::currentDlg = nullptr;
InterfaceSettingsDialog::InterfaceSettingsDialog(QWidget* parent) : QDialog(parent), ui(new Ui::InterfaceSettingsDialog)
{
    ui->setupUi(this);
    setAttribute(Qt::WA_DeleteOnClose);

    mainWindow = (MainWindow*)parent;
    emuInstance = mainWindow->getEmuInstance();

    auto& cfg = emuInstance->getGlobalConfig();

    ui->cbMouseHide->setChecked(cfg.GetBool("Mouse.Hide"));
    ui->spinMouseHideSeconds->setEnabled(ui->cbMouseHide->isChecked());
    ui->spinMouseHideSeconds->setValue(cfg.GetInt("Mouse.HideSeconds"));
    ui->cbPauseLostFocus->setChecked(cfg.GetBool("PauseLostFocus"));
    ui->cbMuteFastForward->setChecked(cfg.GetBool("MuteFastForward"));
    ui->cbDesktopShortcuts->setChecked(cfg.GetBool("Library.DesktopShortcuts"));
    ui->spinTargetFPS->setValue(cfg.GetDouble("TargetFPS"));
    ui->spinFFW->setValue(cfg.GetDouble("FastForwardFPS"));
    ui->spinSlow->setValue(cfg.GetDouble("SlowmoFPS"));

    const QList<QString> themeKeys = QStyleFactory::keys();
    const QString currentTheme = qApp->style()->objectName();
    QString cfgTheme = cfg.GetQString("UITheme");

    ui->cbxUITheme->addItem("System default", "");

    for (int i = 0; i < themeKeys.length(); i++)
    {
        ui->cbxUITheme->addItem(themeKeys[i], themeKeys[i]);
        if (!cfgTheme.isEmpty() && themeKeys[i].compare(currentTheme, Qt::CaseInsensitive) == 0)
            ui->cbxUITheme->setCurrentIndex(i + 1);
    }

    QString cfgQSSTheme = cfg.GetQString("UIQSSTheme");
    ui->cbxQSSTheme->addItem("Dark Glass", "dark_glass");
    ui->cbxQSSTheme->addItem("Neo Modern", "neo_modern");
    if (cfgQSSTheme == "neo_modern")
        ui->cbxQSSTheme->setCurrentIndex(1);
    else
        ui->cbxQSSTheme->setCurrentIndex(0);

    ui->cbxIxraniumColor->addItem("None", "");
    ui->cbxIxraniumColor->addItem("Blue", "ixranium_blue");
    ui->cbxIxraniumColor->addItem("Red", "ixranium_red");
    ui->cbxIxraniumColor->addItem("Green", "ixranium_green");
    ui->cbxIxraniumColor->addItem("Purple", "ixranium_purple");
    int ixraniumIdx = ui->cbxIxraniumColor->findData(cfgQSSTheme);
    ui->cbxIxraniumColor->setCurrentIndex(ixraniumIdx >= 0 ? ixraniumIdx : 0);

    // Available UI languages. "" (System default) uses the OS locale, like before.
    // Add new entries here whenever a new translations/melonDS_XX.ts is added.
    static const QList<QPair<QString, QString>> languages = {
        { QObject::tr("System default"), "" },
        { QStringLiteral("English"),      "en" },
        { QStringLiteral("Türkçe"),       "tr" },
    };

    QString cfgLang = cfg.GetQString("Language");

    for (int i = 0; i < languages.length(); i++)
    {
        ui->cbxUILanguage->addItem(languages[i].first, languages[i].second);
        if (languages[i].second == cfgLang)
            ui->cbxUILanguage->setCurrentIndex(i);
    }
}

InterfaceSettingsDialog::~InterfaceSettingsDialog()
{
    delete ui;
}

void InterfaceSettingsDialog::on_cbMouseHide_clicked()
{
    ui->spinMouseHideSeconds->setEnabled(ui->cbMouseHide->isChecked());
}

void InterfaceSettingsDialog::on_pbClean_clicked()
{
    ui->spinTargetFPS->setValue(60.0000);
}

void InterfaceSettingsDialog::on_pbAccurate_clicked()
{
    ui->spinTargetFPS->setValue(59.8261);
}

void InterfaceSettingsDialog::on_pb2x_clicked()
{
    ui->spinFFW->setValue(ui->spinTargetFPS->value() * 2.0);
}

void InterfaceSettingsDialog::on_pb3x_clicked()
{
    ui->spinFFW->setValue(ui->spinTargetFPS->value() * 3.0);
}

void InterfaceSettingsDialog::on_pbMAX_clicked()
{
    ui->spinFFW->setValue(1000.0);
}

void InterfaceSettingsDialog::on_pbHalf_clicked()
{
    ui->spinSlow->setValue(ui->spinTargetFPS->value() / 2.0);
}

void InterfaceSettingsDialog::on_pbQuarter_clicked()
{
    ui->spinSlow->setValue(ui->spinTargetFPS->value() / 4.0);
}

void InterfaceSettingsDialog::on_cbxUILanguage_currentIndexChanged(int index)
{
    Q_UNUSED(index);
    // Language changes only take effect after restarting melonDS.
    ui->lblLanguageNote->setVisible(true);
}

void InterfaceSettingsDialog::done(int r)
{
    if (!mainWindow->getEmuInstance())
    {
        QDialog::done(r);
        closeDlg();
        return;
    }

    if (r == QDialog::Accepted)
    {
        auto& cfg = emuInstance->getGlobalConfig();

        cfg.SetBool("Mouse.Hide", ui->cbMouseHide->isChecked());
        cfg.SetInt("Mouse.HideSeconds", ui->spinMouseHideSeconds->value());
        cfg.SetBool("PauseLostFocus", ui->cbPauseLostFocus->isChecked());
        cfg.SetBool("MuteFastForward", ui->cbMuteFastForward->isChecked());
        cfg.SetBool("Library.DesktopShortcuts", ui->cbDesktopShortcuts->isChecked());

        double val = ui->spinTargetFPS->value();
        if (val == 0.0) cfg.SetDouble("TargetFPS", 0.0001);
        else cfg.SetDouble("TargetFPS", val);
        
        val = ui->spinFFW->value();
        if (val == 0.0) cfg.SetDouble("FastForwardFPS", 0.0001);
        else cfg.SetDouble("FastForwardFPS", val);
        
        val = ui->spinSlow->value();
        if (val == 0.0) cfg.SetDouble("SlowmoFPS", 0.0001);
        else cfg.SetDouble("SlowmoFPS", val);

        QString themeName = ui->cbxUITheme->currentData().toString();
        cfg.SetQString("UITheme", themeName);

        QString ixraniumColor = ui->cbxIxraniumColor->currentData().toString();
        QString qssThemeName = !ixraniumColor.isEmpty() ? ixraniumColor : ui->cbxQSSTheme->currentData().toString();
        cfg.SetQString("UIQSSTheme", qssThemeName);
        LibraryScreen::ApplyAccentTheme(qssThemeName);

        QString langCode = ui->cbxUILanguage->currentData().toString();
        cfg.SetQString("Language", langCode);

        Config::Save();

        if (!themeName.isEmpty())
            qApp->setStyle(themeName);
        else
            qApp->setStyle(*systemThemeName);
        applyPopupCornerFix();

        {
            QFile qssFile(":/" + qssThemeName);
            if (qssFile.open(QFile::ReadOnly | QFile::Text))
            {
                QTextStream qssStream(&qssFile);
                qApp->setStyleSheet(qssStream.readAll());
                qssFile.close();
            }
        }

        emit updateInterfaceSettings();
    }

    QDialog::done(r);

    closeDlg();
}
