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

#include "WelcomeDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFont>

WelcomeDialog::WelcomeDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(tr("Welcome to MelonDS - Ixranium Fork"));
    setModal(true);
    setMinimumWidth(380);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 20);
    root->setSpacing(16);

    auto* heading = new QLabel(tr("Welcome!"), this);
    QFont headingFont = heading->font();
    headingFont.setPointSize(headingFont.pointSize() + 6);
    headingFont.setBold(true);
    heading->setFont(headingFont);
    root->addWidget(heading);

    auto* subtitle = new QLabel(tr("Let's get a couple of things set up before you start."), this);
    subtitle->setWordWrap(true);
    root->addWidget(subtitle);

    auto* nameLabel = new QLabel(tr("What should we call you?"), this);
    root->addWidget(nameLabel);

    nameEdit = new QLineEdit(this);
    nameEdit->setPlaceholderText(tr("Nickname"));
    nameEdit->setMaxLength(10); // matches the DS firmware username limit
    root->addWidget(nameEdit);

    auto* langLabel = new QLabel(tr("Interface language"), this);
    root->addWidget(langLabel);

    languageBox = new QComboBox(this);
    // Same list/values as InterfaceSettingsDialog::cbxUILanguage - add new
    // entries here (and there) whenever a new translations/melonDS_XX.ts
    // is added.
    languageBox->addItem(tr("System default"), "");
    languageBox->addItem(QStringLiteral("English"), "en");
    languageBox->addItem(QStringLiteral("Türkçe"), "tr");
    root->addWidget(languageBox);

    root->addSpacing(4);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->addStretch(1);
    auto* startBtn = new QPushButton(tr("Get started"), this);
    startBtn->setObjectName("primaryButton");
    startBtn->setDefault(true);
    connect(startBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttonRow->addWidget(startBtn);
    root->addLayout(buttonRow);
}

QString WelcomeDialog::chosenName() const
{
    return nameEdit->text().trimmed();
}

QString WelcomeDialog::chosenLanguageCode() const
{
    return languageBox->currentData().toString();
}
