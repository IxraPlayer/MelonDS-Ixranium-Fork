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

#ifndef CONTROLSCHEMESTORE_H
#define CONTROLSCHEMESTORE_H

#include <QString>
#include <QStringList>

// Persistent, user-named keyboard control schemes (12-button keypad mapping,
// in EmuInstance::buttonNames[] native order). Stored in QSettings under
// their own "ControlSchemes" array, independent of the built-in ready-made
// presets in InputConfigDialog.h and of the per-instance melonDS config -
// a scheme is shared across all instances/profiles once saved.
namespace ControlSchemeStore
{
    // Names of every saved scheme, in the order they were created/saved.
    QStringList listNames();

    // Saves (or overwrites, if the name already exists) a scheme.
    void save(const QString& name, const int (&nativeKeyMap)[12]);

    // Loads a scheme's mapping into nativeKeyMap. Returns false if no
    // scheme with that name exists (nativeKeyMap is left untouched).
    bool load(const QString& name, int (&nativeKeyMap)[12]);

    // Deletes a saved scheme. No-op if it doesn't exist.
    void remove(const QString& name);
}

#endif // CONTROLSCHEMESTORE_H
