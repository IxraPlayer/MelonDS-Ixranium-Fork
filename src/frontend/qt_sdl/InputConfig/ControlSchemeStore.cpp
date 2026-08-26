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

#include "ControlSchemeStore.h"

#include <QSettings>

namespace ControlSchemeStore
{

static const char* kArrayKey = "ControlSchemes";

struct Entry
{
    QString name;
    int map[12];
};

// Reads every stored entry, in order. Shared by all the public functions
// below so the array-rewrite logic (needed for save/remove, since QSettings
// arrays have no in-place delete or update-by-name) only lives in one place.
static QVector<Entry> readAll(QSettings& settings)
{
    QVector<Entry> entries;
    int count = settings.beginReadArray(kArrayKey);
    for (int i = 0; i < count; i++)
    {
        settings.setArrayIndex(i);
        Entry e;
        e.name = settings.value("name").toString();
        for (int k = 0; k < 12; k++)
            e.map[k] = settings.value(QString("k%1").arg(k), -1).toInt();
        entries.append(e);
    }
    settings.endArray();
    return entries;
}

static void writeAll(QSettings& settings, const QVector<Entry>& entries)
{
    settings.remove(kArrayKey);
    settings.beginWriteArray(kArrayKey);
    for (int i = 0; i < entries.size(); i++)
    {
        settings.setArrayIndex(i);
        settings.setValue("name", entries[i].name);
        for (int k = 0; k < 12; k++)
            settings.setValue(QString("k%1").arg(k), entries[i].map[k]);
    }
    settings.endArray();
}

QStringList listNames()
{
    QSettings settings;
    QStringList names;
    for (const Entry& e : readAll(settings))
        names.append(e.name);
    return names;
}

void save(const QString& name, const int (&nativeKeyMap)[12])
{
    QSettings settings;
    QVector<Entry> entries = readAll(settings);

    int idx = -1;
    for (int i = 0; i < entries.size(); i++)
    {
        if (entries[i].name == name) { idx = i; break; }
    }

    Entry e;
    e.name = name;
    for (int k = 0; k < 12; k++)
        e.map[k] = nativeKeyMap[k];

    if (idx >= 0)
        entries[idx] = e;
    else
        entries.append(e);

    writeAll(settings, entries);
}

bool load(const QString& name, int (&nativeKeyMap)[12])
{
    QSettings settings;
    for (const Entry& e : readAll(settings))
    {
        if (e.name != name) continue;
        for (int k = 0; k < 12; k++)
            nativeKeyMap[k] = e.map[k];
        return true;
    }
    return false;
}

void remove(const QString& name)
{
    QSettings settings;
    QVector<Entry> entries = readAll(settings);
    for (int i = 0; i < entries.size(); i++)
    {
        if (entries[i].name == name)
        {
            entries.remove(i);
            break;
        }
    }
    writeAll(settings, entries);
}

}
