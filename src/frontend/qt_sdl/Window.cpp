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

#include "NDS.h"
#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <string.h>

#include <optional>
#include <vector>
#include <string>
#include <algorithm>

#include <QProcess>
#include <QApplication>
#include <QPalette>
#include <QMessageBox>
#include <QMenuBar>
#include <QMimeDatabase>
#include <QFileDialog>
#include <QInputDialog>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>
#include <QSet>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <objbase.h>
#endif
#if defined(Q_OS_LINUX)
#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h> // QNativeInterface::QX11Application
#include <X11/Xlib.h>
#include <X11/Xatom.h>
// Xlib.h #defines plain identifiers like None/Bool/Status/Unsorted/etc as
// preprocessor macros. Every Qt header included AFTER this point that uses
// those words as an enumerator name (e.g. QStyleOptionTab::CornerWidgets'
// "None", QAbstractItemView's "Unsorted", QStyleOptionToolButton's "Bool"-
// adjacent members) gets silently text-substituted before the compiler ever
// sees the real identifier, which is what produces the cascade of
// "has not been declared" / "template argument 1 is invalid" errors in
// qstyleoption.h and everything that transitively includes it
// (QListWidget, QTableWidget, QToolBar, ...). Undefine the offending X11
// macros immediately after pulling in Xlib/Xatom so they can't leak into
// any later header.
#undef None
#undef Bool
#undef Status
#undef Unsorted
#undef CursorShape
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
#undef FontChange
#undef Expose
#undef Above
#undef Below
#undef Complex
#endif
#include <QHBoxLayout>
#include <QLabel>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QPainterPath>
#include <QRegion>
#include <QTimer>
#include <QKeyEvent>
#include <functional>
#include <QMimeData>
#include <QVector>
#include <QCommandLineParser>
#include <QDesktopServices>
#include <QDir>
#include <QCheckBox>
#include <QFile>
#include <QCryptographicHash>
#include <QTextStream>
#include <QRegularExpression>
#include <QStandardPaths>

#include "main.h"
#include "CheatsDialog.h"
#include "DateTimeDialog.h"
#include "EmuSettingsDialog.h"
#include "SettingsHubDialog.h"
#include "LibraryScreen.h"
#include "WelcomeDialog.h"
#include "InputConfig/InputConfigDialog.h"
#include "VideoSettingsDialog.h"
#include "CameraSettingsDialog.h"
#include "AudioSettingsDialog.h"
#include "FirmwareSettingsDialog.h"
#include "PathSettingsDialog.h"
#include "DebugSettingsDialog.h"
#include "MPSettingsDialog.h"
#include "WifiSettingsDialog.h"
#include "InterfaceSettingsDialog.h"
#include "ROMInfoDialog.h"
#include "RAMInfoDialog.h"
#include "TitleManagerDialog.h"
#include "PowerManagement/PowerManagementDialog.h"

#include "Platform.h"
#include "Config.h"
#include "version.h"
#include "Savestate.h"
#include "MPInterface.h"
#include "LANDialog.h"

//#include "main_shaders.h"

#include "EmuInstance.h"
#include "ArchiveUtil.h"
#include "CameraManager.h"
#include "Window.h"
#include "AboutDialog.h"
#include "CustomTitleBar.h"
#include "TopMenuBar.h"
#include "InputConfig/KeyboardPreviewWidget.h"
#include "InputConfig/ControlSchemeStore.h"
#include <QToolBar>
#include <QWindow>

using namespace melonDS;




extern CameraManager* camManager[2];
extern bool camStarted[2];


QString NdsRomMimeType = "application/x-nintendo-ds-rom";
QStringList NdsRomExtensions { ".nds", ".srl", ".dsi", ".ids" };

QString GbaRomMimeType = "application/x-gba-rom";
QStringList GbaRomExtensions { ".gba", ".agb" };


// This list of supported archive formats is based on libarchive(3) version 3.6.2 (2022-12-09).
QStringList ArchiveMimeTypes
{
#ifdef ARCHIVE_SUPPORT_ENABLED
    "application/zip",
    "application/x-7z-compressed",
    "application/vnd.rar", // *.rar
    "application/x-tar",

    "application/x-compressed-tar", // *.tar.gz
    "application/x-xz-compressed-tar",
    "application/x-bzip-compressed-tar",
    "application/x-lz4-compressed-tar",
    "application/x-zstd-compressed-tar",

    "application/x-tarz", // *.tar.Z
    "application/x-lzip-compressed-tar",
    "application/x-lzma-compressed-tar",
    "application/x-lrzip-compressed-tar",
    "application/x-tzo", // *.tar.lzo
#endif
};

QStringList ArchiveExtensions
{
#ifdef ARCHIVE_SUPPORT_ENABLED
    ".zip", ".7z", ".rar", ".tar",

    ".tar.gz", ".tgz",
    ".tar.xz", ".txz",
    ".tar.bz2", ".tbz2",
    ".tar.lz4", ".tlz4",
    ".tar.zst", ".tzst",

    ".tar.Z", ".taz",
    ".tar.lz",
    ".tar.lzma", ".tlz",
    ".tar.lrz", ".tlrz",
    ".tar.lzo", ".tzo"
#endif
};

// AAAAAAA
static bool FileExtensionInList(const QString& filename, const QStringList& extensions, Qt::CaseSensitivity cs = Qt::CaseInsensitive)
{
    return std::any_of(extensions.cbegin(), extensions.cend(), [&](const auto& ext) {
        return filename.endsWith(ext, cs);
    });
}

static bool MimeTypeInList(const QMimeType& mimetype, const QStringList& superTypeNames)
{
    return std::any_of(superTypeNames.cbegin(), superTypeNames.cend(), [&](const auto& superTypeName) {
        return mimetype.inherits(superTypeName);
    });
}


static bool NdsRomByExtension(const QString& filename)
{
    return FileExtensionInList(filename, NdsRomExtensions);
}

static bool GbaRomByExtension(const QString& filename)
{
    return FileExtensionInList(filename, GbaRomExtensions);
}

static bool SupportedArchiveByExtension(const QString& filename)
{
    return FileExtensionInList(filename, ArchiveExtensions);
}


static bool NdsRomByMimetype(const QMimeType& mimetype)
{
    return mimetype.inherits(NdsRomMimeType);
}

static bool GbaRomByMimetype(const QMimeType& mimetype)
{
    return mimetype.inherits(GbaRomMimeType);
}

static bool SupportedArchiveByMimetype(const QMimeType& mimetype)
{
    return MimeTypeInList(mimetype, ArchiveMimeTypes);
}

static bool ZstdNdsRomByExtension(const QString& filename)
{
    return filename.endsWith(".zst", Qt::CaseInsensitive) &&
        NdsRomByExtension(filename.left(filename.size() - 4));
}

static bool ZstdGbaRomByExtension(const QString& filename)
{
    return filename.endsWith(".zst", Qt::CaseInsensitive) &&
        GbaRomByExtension(filename.left(filename.size() - 4));
}

static bool FileIsSupportedFiletype(const QString& filename, bool insideArchive = false)
{
    if (ZstdNdsRomByExtension(filename) || ZstdGbaRomByExtension(filename))
        return true;

    if (NdsRomByExtension(filename) || GbaRomByExtension(filename) || SupportedArchiveByExtension(filename))
        return true;

    const auto matchmode = insideArchive ? QMimeDatabase::MatchExtension : QMimeDatabase::MatchDefault;
    const QMimeType mimetype = QMimeDatabase().mimeTypeForFile(filename, matchmode);
    return NdsRomByMimetype(mimetype) || GbaRomByMimetype(mimetype) || SupportedArchiveByMimetype(mimetype);
}



#if defined(Q_OS_LINUX)
// Qt::FramelessWindowHint / Qt::CustomizeWindowHint are a *request* to the
// window manager - Qt translates them into WM hints internally, but some
// lightweight/non-compositing WMs (Openbox and similar) don't fully honor
// that translation and still paint their own titlebar + border decoration
// on top of ours. That's what caused the second/duplicate titlebar and the
// rounded corners on the outer window edge in bug reports: those corners
// and that top bar belong to the WM's decoration, not to anything this app
// paints, so no amount of changing our own QSS/paintEvent can remove them.
//
// _MOTIF_WM_HINTS is the actual X11 property nearly every WM (including
// Openbox) checks to decide whether to decorate a window at all, so we set
// it directly instead of relying on Qt's flag translation reaching it.
static void forceX11Undecorated(QWidget* window)
{
    if (QGuiApplication::platformName() != "xcb")
        return; // this property is an X11-ism; Wayland sessions don't use it

    WId wid = window->winId(); // forces creation of the native X11 window

    Display* dpy = XOpenDisplay(nullptr);
    if (!dpy)
        return;

    Atom mwmHintsAtom = XInternAtom(dpy, "_MOTIF_WM_HINTS", False);
    struct
    {
        unsigned long flags;
        unsigned long functions;
        unsigned long decorations;
        long input_mode;
        unsigned long status;
    } hints = { 1L << 1 /* MWM_HINTS_DECORATIONS */, 0, 0 /* no decorations */, 0, 0 };

    XChangeProperty(dpy, wid, mwmHintsAtom, mwmHintsAtom, 32, PropModeReplace,
                     reinterpret_cast<unsigned char*>(&hints), 5);

    XFlush(dpy);
    XCloseDisplay(dpy);
}
#endif

MainWindow::MainWindow(int id, EmuInstance* inst, QWidget* parent) :
    QMainWindow(parent),
    windowID(id),
    emuInstance(inst),
    globalCfg(inst->globalCfg),
    localCfg(inst->localCfg),
    windowCfg(localCfg.GetTable("Window"+std::to_string(id), "Window0")),
    emuThread(inst->getEmuThread()),
    enabledSaved(false),
    focused(true)
{

    showOSD = windowCfg.GetBool("ShowOSD");
    showKeyboardPreview = windowCfg.GetBool("ShowKeyboardPreview");

    setWindowTitle("MelonDS - Ixranium Fork " MELONDS_VERSION);
    setAttribute(Qt::WA_DeleteOnClose);
    setAcceptDrops(true);
    setFocusPolicy(Qt::ClickFocus);

    // Custom title bar: drop the OS decorations and draw our own so the
    // minimize/maximize/close buttons match the rest of the panel styling.
    // CustomizeWindowHint is set alongside FramelessWindowHint because on
    // some X11 window managers (lightweight/non-compositing ones, e.g.
    // Openbox-based setups) FramelessWindowHint alone is not enough to
    // suppress the WM's own titlebar/border decoration - the WM still
    // decorates the window and, on top of that, its shadow/corner-rounding
    // compositing effect (meant for normal decorated windows) gets applied
    // to it too, which is what shows up as the black corner artifacts.
    // CustomizeWindowHint makes the "this app is undecorated" intent
    // explicit rather than relying on FramelessWindowHint's default
    // behavior, which is what those WMs actually check for.
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint | Qt::CustomizeWindowHint);
#if defined(Q_OS_WIN)
    // Qt's frameless hints don't reliably strip the native WS_CAPTION style
    // bit on Windows -- it can stay invisible while restored but comes back
    // as a real second titlebar (drawn by DWM) once the window is maximized,
    // stacking above our own CustomTitleBar. Clearing WS_CAPTION directly on
    // the HWND (keeping WS_THICKFRAME so resize/snap/aero-shadow still work)
    // removes it for good, in both restored and maximized states. winId()
    // forces the native handle to exist so this can be applied immediately,
    // before the window is ever shown.
    {
        HWND hwnd = reinterpret_cast<HWND>(winId());
        LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
        style &= ~WS_CAPTION;
        style |= WS_THICKFRAME;
        SetWindowLongPtr(hwnd, GWL_STYLE, style);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
#endif
#if defined(Q_OS_LINUX)
    // Belt-and-suspenders for WMs (Openbox, etc.) that don't fully honor
    // the flags above: tell X11 directly, via the property those WMs
    // actually check, not to decorate this window at all. This is what
    // removes the WM's own titlebar/rounded-corner decoration that was
    // showing up doubled above our own custom titlebar.
    forceX11Undecorated(this);
#endif
    // Both previous attempts at this (setMask() clipping, then
    // WA_TranslucentBackground) assumed the desktop compositor blends
    // per-pixel window transparency correctly. On this system it doesn't
    // reliably: without an active compositing manager (common on
    // lightweight setups like this one), pixels Qt marks as "transparent"
    // don't get blended against the desktop at all - they render as
    // whatever garbage/black was already in that buffer, and which pixels
    // that affects varies frame to frame. That's the actual cause of both
    // "still square in some spots" and "top bar suddenly went fully
    // see-through": we were relying on a blending step that this system
    // isn't guaranteed to perform.
    //
    // So: give up on real per-pixel transparency for this window entirely.
    // paintEvent() below draws the whole panel itself - flat fill plus the
    // rounded silhouette - deliberately WITHOUT WA_StyledBackground. Qt
    // paints WA_StyledBackground's QSS panel during the erase pass that
    // runs BEFORE paintEvent() is called, so our own paintEvent drawing
    // was covering/erasing it every frame, which is why corners went
    // perfectly square with no rounded cue at all. Doing 100% of the
    // painting ourselves, in one place, in the right order, means there's
    // nothing left to race or get drawn over.

#if QT_VERSION_MAJOR == 6 && WIN32
    // The "windows11" theme has pretty massive padding around menubar items, this makes Config and Help not fit in a window at 1x screen sizing
    // So let's reduce the padding a bit.
    if (QApplication::style()->name() == "windows11")
        setStyleSheet("QMenuBar::item { padding: 4px 8px; }");
#endif

    //hasMenu = (!parent);
    hasMenu = true;

    if (hasMenu)
    {
        QMenuBar * menubar = new QMenuBar();
        {
            QMenu * menu = menubar->addMenu(tr("File"));

            actOpenROM = menu->addAction(tr("Open ROM..."));
            connect(actOpenROM, &QAction::triggered, this, &MainWindow::onOpenFile);
            actOpenROM->setShortcut(QKeySequence(QKeySequence::StandardKey::Open));

            /*actOpenROMArchive = menu->addAction(tr("Open ROM inside archive..."));
            connect(actOpenROMArchive, &QAction::triggered, this, &MainWindow::onOpenFileArchive);
            actOpenROMArchive->setShortcut(QKeySequence(Qt::Key_O | Qt::CTRL | Qt::SHIFT));*/

            recentMenu = menu->addMenu(tr("Open recent"));
            loadRecentFilesMenu(true);

            //actBootFirmware = menu->addAction(tr("Launch DS menu"));
            actBootFirmware = menu->addAction(tr("Boot firmware"));
            connect(actBootFirmware, &QAction::triggered, this, &MainWindow::onBootFirmware);

            menu->addSeparator();

            actCurrentCart = menu->addAction(tr("DS slot: ") + emuInstance->cartLabel());
            actCurrentCart->setEnabled(false);

            actInsertCart = menu->addAction(tr("Insert cart..."));
            connect(actInsertCart, &QAction::triggered, this, &MainWindow::onInsertCart);

            actEjectCart = menu->addAction(tr("Eject cart"));
            connect(actEjectCart, &QAction::triggered, this, &MainWindow::onEjectCart);

            menu->addSeparator();

            actCurrentGBACart = menu->addAction(tr("GBA slot: ") + emuInstance->gbaCartLabel());
            actCurrentGBACart->setEnabled(false);

            actInsertGBACart = menu->addAction(tr("Insert ROM cart..."));
            connect(actInsertGBACart, &QAction::triggered, this, &MainWindow::onInsertGBACart);

            {
                QMenu * submenu = menu->addMenu(tr("Insert add-on cart"));
                QAction *act;

                int addons[] = {
                    GBAAddon_RAMExpansion,
                    GBAAddon_RumblePak,
                    GBAAddon_SolarSensorBoktai1,
                    GBAAddon_SolarSensorBoktai2,
                    GBAAddon_SolarSensorBoktai3,
                    GBAAddon_MotionPakHomebrew,
                    GBAAddon_MotionPakRetail,
                    GBAAddon_GuitarGrip,
                    -1
                };

                for (int i = 0; addons[i] != -1; i++)
                {
                    int addon = addons[i];
                    act = submenu->addAction(emuInstance->gbaAddonName(addon));
                    act->setData(QVariant(addon));
                    connect(act, &QAction::triggered, this, &MainWindow::onInsertGBAAddon);
                    actInsertGBAAddon.append(act);
                }
            }

            actEjectGBACart = menu->addAction(tr("Eject cart"));
            connect(actEjectGBACart, &QAction::triggered, this, &MainWindow::onEjectGBACart);

            menu->addSeparator();

            actImportSavefile = menu->addAction(tr("Import savefile"));
            connect(actImportSavefile, &QAction::triggered, this, &MainWindow::onImportSavefile);

            menu->addSeparator();

            {
                QMenu * submenu = menu->addMenu(tr("Save state"));

                for (int i = 1; i < 9; i++)
                {
                    actSaveState[i] = submenu->addAction(QString("%1").arg(i));
                    actSaveState[i]->setShortcut(QKeySequence(Qt::ShiftModifier | (Qt::Key_F1 + i - 1)));
                    actSaveState[i]->setData(QVariant(i));
                    connect(actSaveState[i], &QAction::triggered, this, &MainWindow::onSaveState);
                }

                actSaveState[0] = submenu->addAction(tr("File..."));
                actSaveState[0]->setShortcut(QKeySequence(Qt::ShiftModifier | Qt::Key_F9));
                actSaveState[0]->setData(QVariant(0));
                connect(actSaveState[0], &QAction::triggered, this, &MainWindow::onSaveState);
            }
            {
                QMenu * submenu = menu->addMenu(tr("Load state"));

                for (int i = 1; i < 9; i++)
                {
                    actLoadState[i] = submenu->addAction(QString("%1").arg(i));
                    actLoadState[i]->setShortcut(QKeySequence(Qt::Key_F1 + i - 1));
                    actLoadState[i]->setData(QVariant(i));
                    connect(actLoadState[i], &QAction::triggered, this, &MainWindow::onLoadState);
                }

                actLoadState[0] = submenu->addAction(tr("File..."));
                actLoadState[0]->setShortcut(QKeySequence(Qt::Key_F9));
                actLoadState[0]->setData(QVariant(0));
                connect(actLoadState[0], &QAction::triggered, this, &MainWindow::onLoadState);
            }

            actUndoStateLoad = menu->addAction(tr("Undo state load"));
            actUndoStateLoad->setShortcut(QKeySequence(Qt::Key_F12));
            connect(actUndoStateLoad, &QAction::triggered, this, &MainWindow::onUndoStateLoad);

            menu->addSeparator();
            actOpenConfig = menu->addAction(tr("Open MelonDS - Ixranium Fork directory"));
            connect(actOpenConfig, &QAction::triggered, this, [&]()
            {
                QDesktopServices::openUrl(QUrl::fromLocalFile(emuDirectory));
            });

            menu->addSeparator();

            actQuit = menu->addAction(tr("Quit"));
            connect(actQuit, &QAction::triggered, this, &MainWindow::onQuit);
            actQuit->setShortcut(QKeySequence(QKeySequence::StandardKey::Quit));
        }
        {
            QMenu * menu = menubar->addMenu(tr("System"));

            actPause = menu->addAction(tr("Pause"));
            actPause->setCheckable(true);
            connect(actPause, &QAction::triggered, this, &MainWindow::onPause);

            actReset = menu->addAction(tr("Reset"));
            connect(actReset, &QAction::triggered, this, &MainWindow::onReset);

            actStop = menu->addAction(tr("Stop"));
            connect(actStop, &QAction::triggered, this, &MainWindow::onStop);

            actFrameStep = menu->addAction(tr("Frame step"));
            connect(actFrameStep, &QAction::triggered, this, &MainWindow::onFrameStep);

            menu->addSeparator();

            actPowerManagement = menu->addAction(tr("Power management"));
            connect(actPowerManagement, &QAction::triggered, this, &MainWindow::onOpenPowerManagement);

            actDateTime = menu->addAction(tr("Date and time"));
            connect(actDateTime, &QAction::triggered, this, &MainWindow::onOpenDateTime);

            menu->addSeparator();

            actEnableCheats = menu->addAction(tr("Enable cheats"));
            actEnableCheats->setCheckable(true);
            connect(actEnableCheats, &QAction::triggered, this, &MainWindow::onEnableCheats);

            //if (inst == 0)
            {
                actSetupCheats = menu->addAction(tr("Setup cheat codes"));
                actSetupCheats->setMenuRole(QAction::NoRole);
                connect(actSetupCheats, &QAction::triggered, this, &MainWindow::onSetupCheats);

                menu->addSeparator();
                actROMInfo = menu->addAction(tr("ROM info"));
                connect(actROMInfo, &QAction::triggered, this, &MainWindow::onROMInfo);

                actRAMInfo = menu->addAction(tr("RAM search"));
                connect(actRAMInfo, &QAction::triggered, this, &MainWindow::onRAMInfo);

                actTitleManager = menu->addAction(tr("Manage DSi titles"));
                connect(actTitleManager, &QAction::triggered, this, &MainWindow::onOpenTitleManager);
            }

            {
                menu->addSeparator();
                QMenu * submenu = menu->addMenu(tr("Multiplayer"));

                actMPNewInstance = submenu->addAction(tr("Launch new instance"));
                connect(actMPNewInstance, &QAction::triggered, this, &MainWindow::onMPNewInstance);

                submenu->addSeparator();

                actLANStartHost = submenu->addAction(tr("Host LAN game"));
                connect(actLANStartHost, &QAction::triggered, this, &MainWindow::onLANStartHost);

                actLANStartClient = submenu->addAction(tr("Join LAN game"));
                connect(actLANStartClient, &QAction::triggered, this, &MainWindow::onLANStartClient);

                /*submenu->addSeparator();

                actNPStartHost = submenu->addAction(tr("NETPLAY HOST"));
                connect(actNPStartHost, &QAction::triggered, this, &MainWindow::onNPStartHost);

                actNPStartClient = submenu->addAction(tr("NETPLAY CLIENT"));
                connect(actNPStartClient, &QAction::triggered, this, &MainWindow::onNPStartClient);

                actNPTest = submenu->addAction(tr("NETPLAY GO"));
                connect(actNPTest, &QAction::triggered, this, &MainWindow::onNPTest);*/
            }
        }
        {
            QMenu * menu = menubar->addMenu(tr("View"));

            {
                QMenu * submenu = menu->addMenu(tr("Screen size"));

                for (int i = 0; i < 4; i++)
                {
                    int data = i + 1;
                    actScreenSize[i] = submenu->addAction(QString("%1x").arg(data));
                    actScreenSize[i]->setData(QVariant(data));
                    connect(actScreenSize[i], &QAction::triggered, this, &MainWindow::onChangeScreenSize);
                }
            }
            {
                QMenu * submenu = menu->addMenu(tr("Screen rotation"));
                grpScreenRotation = new QActionGroup(submenu);

                for (int i = 0; i < screenRot_MAX; i++)
                {
                    int data = i * 90;
                    actScreenRotation[i] = submenu->addAction(QString("%1°").arg(data));
                    actScreenRotation[i]->setActionGroup(grpScreenRotation);
                    actScreenRotation[i]->setData(QVariant(i));
                    actScreenRotation[i]->setCheckable(true);
                }

                connect(grpScreenRotation, &QActionGroup::triggered, this, &MainWindow::onChangeScreenRotation);
            }
            {
                QMenu * submenu = menu->addMenu(tr("Screen gap"));
                grpScreenGap = new QActionGroup(submenu);

                const int screengap[] = {0, 1, 8, 64, 90, 128};

                for (int i = 0; i < 6; i++)
                {
                    int data = screengap[i];
                    actScreenGap[i] = submenu->addAction(QString("%1 px").arg(data));
                    actScreenGap[i]->setActionGroup(grpScreenGap);
                    actScreenGap[i]->setData(QVariant(data));
                    actScreenGap[i]->setCheckable(true);
                }

                connect(grpScreenGap, &QActionGroup::triggered, this, &MainWindow::onChangeScreenGap);
            }
            {
                QMenu * submenu = menu->addMenu(tr("Screen layout"));
                grpScreenLayout = new QActionGroup(submenu);

                const char *screenlayout[] = {"Natural", "Vertical", "Horizontal", "Hybrid"};

                for (int i = 0; i < screenLayout_MAX; i++)
                {
                    actScreenLayout[i] = submenu->addAction(QString(screenlayout[i]));
                    actScreenLayout[i]->setActionGroup(grpScreenLayout);
                    actScreenLayout[i]->setData(QVariant(i));
                    actScreenLayout[i]->setCheckable(true);
                }

                connect(grpScreenLayout, &QActionGroup::triggered, this, &MainWindow::onChangeScreenLayout);

                submenu->addSeparator();

                actScreenSwap = submenu->addAction(tr("Swap screens"));
                actScreenSwap->setCheckable(true);
                connect(actScreenSwap, &QAction::triggered, this, &MainWindow::onChangeScreenSwap);
            }
            {
                QMenu * submenu = menu->addMenu(tr("Screen sizing"));
                grpScreenSizing = new QActionGroup(submenu);

                const char *screensizing[] = {"Even", "Emphasize top", "Emphasize bottom", "Auto", "Top only",
                                              "Bottom only"};

                for (int i = 0; i < screenSizing_MAX; i++)
                {
                    actScreenSizing[i] = submenu->addAction(QString(screensizing[i]));
                    actScreenSizing[i]->setActionGroup(grpScreenSizing);
                    actScreenSizing[i]->setData(QVariant(i));
                    actScreenSizing[i]->setCheckable(true);
                }

                connect(grpScreenSizing, &QActionGroup::triggered, this, &MainWindow::onChangeScreenSizing);

                submenu->addSeparator();

                actIntegerScaling = submenu->addAction(tr("Force integer scaling"));
                actIntegerScaling->setCheckable(true);
                connect(actIntegerScaling, &QAction::triggered, this, &MainWindow::onChangeIntegerScaling);
            }
            {
                QMenu * submenu = menu->addMenu(tr("Aspect ratio"));
                grpScreenAspectTop = new QActionGroup(submenu);
                grpScreenAspectBot = new QActionGroup(submenu);
                actScreenAspectTop = new QAction *[AspectRatiosNum];
                actScreenAspectBot = new QAction *[AspectRatiosNum];

                for (int i = 0; i < 2; i++)
                {
                    QActionGroup * group = grpScreenAspectTop;
                    QAction **actions = actScreenAspectTop;

                    if (i == 1)
                    {
                        group = grpScreenAspectBot;
                        submenu->addSeparator();
                        actions = actScreenAspectBot;
                    }

                    for (int j = 0; j < AspectRatiosNum; j++)
                    {
                        auto ratio = aspectRatios[j];
                        QString label = QString("%1 %2").arg(i ? "Bottom" : "Top", ratio.label);
                        actions[j] = submenu->addAction(label);
                        actions[j]->setActionGroup(group);
                        actions[j]->setData(QVariant(ratio.id));
                        actions[j]->setCheckable(true);
                    }

                    connect(group, &QActionGroup::triggered, this, &MainWindow::onChangeScreenAspect);
                }
            }

            menu->addSeparator();

            actNewWindow = menu->addAction(tr("Open new window"));
            connect(actNewWindow, &QAction::triggered, this, &MainWindow::onOpenNewWindow);

            menu->addSeparator();

            actScreenFiltering = menu->addAction(tr("Screen filtering"));
            actScreenFiltering->setCheckable(true);
            connect(actScreenFiltering, &QAction::triggered, this, &MainWindow::onChangeScreenFiltering);

            actShowOSD = menu->addAction(tr("Show OSD"));
            actShowOSD->setCheckable(true);
            connect(actShowOSD, &QAction::triggered, this, &MainWindow::onChangeShowOSD);

            actShowKeyboardPreview = menu->addAction(tr("Show keyboard preview"));
            actShowKeyboardPreview->setCheckable(true);
            connect(actShowKeyboardPreview, &QAction::triggered, this, &MainWindow::onChangeShowKeyboardPreview);
        }
        {
            QMenu * menu = menubar->addMenu(tr("Config"));

            actSettingsHub = menu->addAction(tr("Settings..."));
            connect(actSettingsHub, &QAction::triggered, this, &MainWindow::onOpenSettingsHub);

            actEmuSettings = menu->addAction(tr("Emu settings"));
            connect(actEmuSettings, &QAction::triggered, this, &MainWindow::onOpenEmuSettings);
            menu->removeAction(actEmuSettings);

#ifdef __APPLE__
            actPreferences = menu->addAction(tr("Preferences..."));
            connect(actPreferences, &QAction::triggered, this, &MainWindow::onOpenEmuSettings);
            actPreferences->setMenuRole(QAction::PreferencesRole);
#endif

            actInputConfig = menu->addAction(tr("Input and hotkeys"));
            connect(actInputConfig, &QAction::triggered, this, &MainWindow::onOpenInputConfig);
            menu->removeAction(actInputConfig);

            actVideoSettings = menu->addAction(tr("Video settings"));
            connect(actVideoSettings, &QAction::triggered, this, &MainWindow::onOpenVideoSettings);
            menu->removeAction(actVideoSettings);

            actCameraSettings = menu->addAction(tr("Camera settings"));
            connect(actCameraSettings, &QAction::triggered, this, &MainWindow::onOpenCameraSettings);
            menu->removeAction(actCameraSettings);

            actAudioSettings = menu->addAction(tr("Audio settings"));
            connect(actAudioSettings, &QAction::triggered, this, &MainWindow::onOpenAudioSettings);
            menu->removeAction(actAudioSettings);

            actMPSettings = menu->addAction(tr("Multiplayer settings"));
            connect(actMPSettings, &QAction::triggered, this, &MainWindow::onOpenMPSettings);
            menu->removeAction(actMPSettings);

            actWifiSettings = menu->addAction(tr("Wifi settings"));
            connect(actWifiSettings, &QAction::triggered, this, &MainWindow::onOpenWifiSettings);
            menu->removeAction(actWifiSettings);

            actFirmwareSettings = menu->addAction(tr("Firmware settings"));
            connect(actFirmwareSettings, &QAction::triggered, this, &MainWindow::onOpenFirmwareSettings);
            menu->removeAction(actFirmwareSettings);

            actInterfaceSettings = menu->addAction(tr("Interface settings"));
            connect(actInterfaceSettings, &QAction::triggered, this, &MainWindow::onOpenInterfaceSettings);
            menu->removeAction(actInterfaceSettings);

            actPathSettings = menu->addAction(tr("Path settings"));
            connect(actPathSettings, &QAction::triggered, this, &MainWindow::onOpenPathSettings);
            menu->removeAction(actPathSettings);

            menu->addSeparator();

            actLimitFramerate = menu->addAction(tr("Limit framerate"));
            actLimitFramerate->setCheckable(true);
            connect(actLimitFramerate, &QAction::triggered, this, &MainWindow::onChangeLimitFramerate);

            actAudioSync = menu->addAction(tr("Audio sync"));
            actAudioSync->setCheckable(true);
            connect(actAudioSync, &QAction::triggered, this, &MainWindow::onChangeAudioSync);
        }
        {
            QMenu * menu = menubar->addMenu(tr("Help"));
            actAbout = menu->addAction(tr("About..."));
            connect(actAbout, &QAction::triggered, this, [&]
            {
                auto dialog = AboutDialog(this);
                dialog.exec();
            });
        }

        setMenuBar(menubar);
        // The native menu bar stays alive (actions/shortcuts/checked-state
        // all live on it) but isn't shown -- the visible menu row is our
        // own TopMenuBar, built from the same QMenus above.
        // setFixedHeight(0) alone still left a visible gap: QMainWindow
        // always docks the menu bar above every toolbar (including our
        // CustomTitleBar toolbar), and the native style's own menu-bar
        // frame/panel padding was rendered regardless of the widget's
        // content height, showing up as an empty black strip above the
        // title bar. Actually hiding it removes it from QMainWindow's
        // layout entirely, so no frame padding is reserved for it at all.
        menubar->setMaximumHeight(0);
        menubar->hide();

        if (localCfg.GetString("Firmware.Username") == "Arisotura")
            actMPNewInstance->setText("Fart");
    }

    // Custom title bar (drag to move, min/max/close) + centered, bigger
    // File/System/View/Config/Help row that grows the hovered entry.
    titleBar = new CustomTitleBar(this, this);
    titleBarToolBar = new QToolBar(this);
    titleBarToolBar->setObjectName("titleBarToolBar");
    titleBarToolBar->setMovable(false);
    titleBarToolBar->setFloatable(false);
    titleBarToolBar->toggleViewAction()->setVisible(false);
    titleBarToolBar->addWidget(titleBar);
    addToolBar(Qt::TopToolBarArea, titleBarToolBar);

    if (hasMenu)
    {
        topMenuBar = new TopMenuBar(this);
        for (QAction* act : menuBar()->actions())
            topMenuBar->addMenuButton(act->text(), act->menu());

        topMenuToolBar = new QToolBar(this);
        topMenuToolBar->setObjectName("topMenuToolBar");
        topMenuToolBar->setMovable(false);
        topMenuToolBar->setFloatable(false);
        topMenuToolBar->toggleViewAction()->setVisible(false);
        topMenuToolBar->addWidget(topMenuBar);
        addToolBarBreak(Qt::TopToolBarArea);
        addToolBar(Qt::TopToolBarArea, topMenuToolBar);

        // Floating "bring the menu back" arrow. Lives directly on the
        // window (not in a toolbar/layout) since it only needs to exist
        // while topMenuToolBar is hidden, floating over whatever's
        // underneath at the top-right corner.
        topMenuRestoreBtn = new QToolButton(this);
        topMenuRestoreBtn->setText(QString::fromUtf8("\xE2\x96\xBC")); // ▼
        topMenuRestoreBtn->setToolTip(tr("Show menu"));
        topMenuRestoreBtn->setCursor(Qt::PointingHandCursor);
        topMenuRestoreBtn->setFocusPolicy(Qt::NoFocus);
        topMenuRestoreBtn->setFixedSize(28, 20);
        topMenuRestoreBtn->setStyleSheet(
            "QToolButton { background: rgba(0,0,0,140); border: none; "
            "border-radius: 4px; color: #d6dae4; font-size: 11px; }"
            "QToolButton:hover { background: rgba(0,0,0,190); color: white; }");
        topMenuRestoreBtn->hide();

        connect(topMenuBar, &TopMenuBar::collapseClicked, this, [this]()
        {
            topMenuToolBar->hide();
            positionTopMenuRestoreBtn();
            topMenuRestoreBtn->show();
            topMenuRestoreBtn->raise();
        });

        connect(topMenuRestoreBtn, &QToolButton::clicked, this, [this]()
        {
            topMenuToolBar->show();
            topMenuRestoreBtn->hide();
        });
    }

    resizeGrips = new WindowResizeGrips(this);

#ifdef Q_OS_MAC
    QPoint screenCenter = screen()->availableGeometry().center();
    QRect frameGeo = frameGeometry();
    frameGeo.moveCenter(screenCenter);
    move(frameGeo.topLeft());
#endif

    std::string geom = windowCfg.GetString("Geometry");
    if (!geom.empty())
    {
        QByteArray raw = QByteArray::fromStdString(geom);
        QByteArray dec = QByteArray::fromBase64(raw, QByteArray::Base64Encoding | QByteArray::AbortOnBase64DecodingErrors);
        if (!dec.isEmpty())
            restoreGeometry(dec);
        // if the window was closed in fullscreen do not restore this
        setWindowState(windowState() & ~Qt::WindowFullScreen);
    }
    show();

    panel = nullptr;
    createScreenPanel();

    if (hasMenu)
    {
        actEjectCart->setEnabled(false);
        actEjectGBACart->setEnabled(false);

        if (globalCfg.GetInt("Emu.ConsoleType") == 1)
        {
            actInsertGBACart->setEnabled(false);
            for (auto act: actInsertGBAAddon)
                act->setEnabled(false);
        }

        for (int i = 0; i < 9; i++)
        {
            actSaveState[i]->setEnabled(false);
            actLoadState[i]->setEnabled(false);
        }
        actUndoStateLoad->setEnabled(false);
        actImportSavefile->setEnabled(false);

        actPause->setEnabled(false);
        actReset->setEnabled(false);
        actStop->setEnabled(false);
        actFrameStep->setEnabled(false);

        //actDateTime->setEnabled(true);
        actPowerManagement->setEnabled(false);

        actEnableCheats->setEnabled(false);
        actSetupCheats->setEnabled(false);
        actTitleManager->setEnabled(!globalCfg.GetString("DSi.NANDPath").empty());

        actEnableCheats->setChecked(localCfg.GetBool("EnableCheats"));

        actROMInfo->setEnabled(false);
        actRAMInfo->setEnabled(false);

        actScreenRotation[windowCfg.GetInt("ScreenRotation")]->setChecked(true);

        int screenGap = windowCfg.GetInt("ScreenGap");
        for (int i = 0; i < 6; i++)
        {
            if (actScreenGap[i]->data().toInt() == screenGap)
            {
                actScreenGap[i]->setChecked(true);
                break;
            }
        }

        actScreenLayout[windowCfg.GetInt("ScreenLayout")]->setChecked(true);
        actScreenSizing[windowCfg.GetInt("ScreenSizing")]->setChecked(true);
        actIntegerScaling->setChecked(windowCfg.GetBool("IntegerScaling"));

        actScreenSwap->setChecked(windowCfg.GetBool("ScreenSwap"));

        int aspectTop = windowCfg.GetInt("ScreenAspectTop");
        int aspectBot = windowCfg.GetInt("ScreenAspectBot");
        for (int i = 0; i < AspectRatiosNum; i++)
        {
            if (aspectTop == aspectRatios[i].id)
                actScreenAspectTop[i]->setChecked(true);
            if (aspectBot == aspectRatios[i].id)
                actScreenAspectBot[i]->setChecked(true);
        }

        actScreenFiltering->setChecked(windowCfg.GetBool("ScreenFilter"));
        actShowOSD->setChecked(showOSD);
        actShowKeyboardPreview->setChecked(showKeyboardPreview);

        actLimitFramerate->setChecked(emuInstance->doLimitFPS);
        actAudioSync->setChecked(emuInstance->doAudioSync);

        if (emuInstance->instanceID > 0)
        {
            actEmuSettings->setEnabled(false);
            actVideoSettings->setEnabled(false);
            actMPSettings->setEnabled(false);
            actWifiSettings->setEnabled(false);
            actInterfaceSettings->setEnabled(false);

#ifdef __APPLE__
            actPreferences->setEnabled(false);
#endif // __APPLE__
        }

        if (emuThread->emuIsActive())
            onEmuStart();
    }

    QObject::connect(qApp, &QApplication::applicationStateChanged, this, &MainWindow::onAppStateChanged);
    onUpdateInterfaceSettings();

    updateMPInterface(MPInterface::GetType());

    // First-run welcome: ask for a nickname and a UI language before the
    // user ever has to go digging through Config for them. Only for the
    // first window of the first instance, and only once ever (gated by the
    // "OnboardingDone" flag) - never shown again after that, including for
    // any extra windows/instances opened later.
    if (windowID == 0 && emuInstance->instanceID == 0 && !globalCfg.GetBool("OnboardingDone"))
    {
        QTimer::singleShot(0, this, [this]()
        {
            WelcomeDialog dlg(this);
            if (dlg.exec() == QDialog::Accepted)
            {
                QString name = dlg.chosenName();
                if (!name.isEmpty())
                {
                    auto& cfg = emuInstance->getLocalConfig();
                    cfg.GetTable("Firmware").SetQString("Username", name);
                }

                globalCfg.SetQString("Language", dlg.chosenLanguageCode());
            }

            // Set regardless of accept/cancel - this is a one-time prompt,
            // not a nag that should keep reappearing.
            globalCfg.SetBool("OnboardingDone", true);
        });
    }

    // In-game keyboard mapping preview (bottom-right corner), independent of
    // the pause menu's own copy - toggled from View > Show keyboard preview.
    //
    // A genuine top-level window rather than a child of the central widget:
    // a plain QWidget overlapping the GL-rendered screen panel forces Qt to
    // recomposite the WHOLE panel (the actual game frame) on every single
    // repaint of this widget - every key press/release during gameplay was
    // causing a full-screen recomposite, which is what was actually behind
    // the stutter/desync. A separate top-level surface is composited by the
    // OS independently and never touches the game's own paint cycle.
    //
    // Passing `this` as the parent (rather than nullptr) still keeps it a
    // real top-level window for that reason, but also makes MainWindow its
    // OS-level *owner*: it stacks above MainWindow specifically (follows
    // it, minimizes/restores with it), instead of floating as a fully
    // independent, ownerless window that can end up behind the game and
    // reads as "a separate app window" to the user. Qt::Tool +
    // WindowDoesNotAcceptFocus additionally keep it out of the taskbar/
    // alt-tab and unable to steal keyboard focus.
    liveKeyboardPreview = new KeyboardPreviewWidget(this);
    liveKeyboardPreview->setWindowFlags(Qt::Tool | Qt::FramelessWindowHint |
                                         Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    // Belt-and-suspenders against a visible rectangular "window" edge:
    // WA_TranslucentBackground alone can still leave Qt's own backing
    // store pre-filling this widget's rect with an opaque color on some
    // platforms/styles before our paintEvent runs, which is exactly what
    // reads as "a separate window" rather than a borderless in-game
    // overlay. NoSystemBackground + disabling auto-fill + a transparent
    // palette make sure nothing but our own hand-drawn keycaps ever paints.
    liveKeyboardPreview->setAttribute(Qt::WA_TranslucentBackground);
    liveKeyboardPreview->setAttribute(Qt::WA_NoSystemBackground);
    liveKeyboardPreview->setAutoFillBackground(false);
    QPalette pal = liveKeyboardPreview->palette();
    pal.setColor(QPalette::Window, Qt::transparent);
    liveKeyboardPreview->setPalette(pal);
    liveKeyboardPreview->setAttribute(Qt::WA_TransparentForMouseEvents);
    liveKeyboardPreview->setAttribute(Qt::WA_ShowWithoutActivating);
    liveKeyboardPreview->setFixedSize(400, 140);
    liveKeyboardPreview->refreshFromInstance(emuInstance);
    positionLiveKeyboardPreview();
    liveKeyboardPreview->setVisible(false); // gameplay-only; see updateLiveKeyboardPreviewVisibility
    updateLiveKeyboardPreviewVisibility();

#if defined(Q_OS_LINUX)
    // Same idea on X11: Qt::Tool usually keeps this out of the taskbar,
    // but some window managers only reliably honor it via the explicit
    // EWMH _NET_WM_STATE_SKIP_TASKBAR/SKIP_PAGER hints. Set those by hand
    // once the native window exists. (Wayland has no equivalent knob;
    // there Qt::Tool is all we can do, and every compositor we've tested
    // already respects it.)
    if (QGuiApplication::platformName() == "xcb")
    {
        liveKeyboardPreview->winId(); // force native X11 window creation now
        if (auto* x11App = qGuiApp->nativeInterface<QNativeInterface::QX11Application>())
        {
            Display* dpy = x11App->display();
            Window w = (Window)liveKeyboardPreview->winId();
            Atom netWmState = XInternAtom(dpy, "_NET_WM_STATE", False);
            Atom skipTaskbar = XInternAtom(dpy, "_NET_WM_STATE_SKIP_TASKBAR", False);
            Atom skipPager = XInternAtom(dpy, "_NET_WM_STATE_SKIP_PAGER", False);
            Atom states[2] = { skipTaskbar, skipPager };
            XChangeProperty(dpy, w, netWmState, XA_ATOM, 32, PropModeAppend,
                             (unsigned char*)states, 2);
        }
    }
#endif
}

MainWindow::~MainWindow()
{
    if (hasMenu)
    {
        delete[] actScreenAspectTop;
        delete[] actScreenAspectBot;
    }

    delete liveKeyboardPreview;
    liveKeyboardPreview = nullptr;
}

void MainWindow::osdAddMessage(unsigned int color, const char* msg)
{
    if (!showOSD) return;
    panel->osdAddMessage(color, msg);
}

void MainWindow::toggleDebugOverlay()
{
    panel->setDebugOverlayVisible(!panel->debugOverlayVisible());
}

void MainWindow::saveEnabled(bool enabled)
{
    if (enabledSaved) return;
    windowCfg.SetBool("Enabled", enabled);
    enabledSaved = true;
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (emuInstance)
    {
        if (windowID == 0)
            emuInstance->saveEnabledWindows();
        else
            saveEnabled(false);
    }

    // explicitly close children windows, so the OpenGL contexts get closed properly
    auto childwins = findChildren<MainWindow *>(nullptr, Qt::FindDirectChildrenOnly);
    for (auto child : childwins)
        child->close();

    if (!emuInstance) return;

    QByteArray geom = saveGeometry();
    QByteArray enc = geom.toBase64(QByteArray::Base64Encoding);
    windowCfg.SetString("Geometry", enc.toStdString());
    Config::Save();

    emuInstance->deleteWindow(windowID, false);

    // emuInstance may be deleted
    // prevent use after free from us
    emuInstance = nullptr;
    QMainWindow::closeEvent(event);
}

void MainWindow::createScreenPanel()
{
    auto oldpanel = panel;
    // The debug overlay's visibility (and its refresh timer) lives on the
    // ScreenPanel instance itself. Recreating the panel (e.g. switching
    // renderers) previously dropped that state silently -- the overlay
    // just stopped updating until manually toggled off/on again.
    bool hadDebugOverlay = oldpanel && oldpanel->debugOverlayVisible();
    panel = nullptr;
    if (oldpanel) delete oldpanel;

    hasOGL = globalCfg.GetBool("Screen.UseGL") ||
            (globalCfg.GetInt("3D.Renderer") != renderer3D_Software);

    if (hasOGL)
    {
        ScreenPanelGL* panelGL = new ScreenPanelGL(this);
        panelGL->show();

        // make sure no GL context is in use by the emu thread
        // otherwise we may fail to create a shared context
        if (windowID != 0)
            emuThread->borrowGL();

        // Check that creating the context hasn't failed
        if (panelGL->createContext() == false)
        {
            Log(Platform::LogLevel::Error, "Failed to create OpenGL context, falling back to Software Renderer.\n");
            hasOGL = false;

            globalCfg.SetBool("Screen.UseGL", false);
            globalCfg.SetInt("3D.Renderer", renderer3D_Software);

            delete panelGL;
            panelGL = nullptr;
        }

        if (windowID != 0)
            emuThread->returnGL();

        panel = panelGL;
    }

    if (!hasOGL)
    {
        ScreenPanelNative* panelNative = new ScreenPanelNative(this);
        panel = panelNative;
        panel->show();
    }

    if (hadDebugOverlay)
        panel->setDebugOverlayVisible(true);

    if (!centralStack)
    {
        centralStack = new QStackedWidget(this);

        library = new LibraryScreen(this);
        centralStack->addWidget(library);
        connect(library, &LibraryScreen::romActivated, this, &MainWindow::onLibraryGameActivated);
        connect(library, &LibraryScreen::addGameRequested, this, &MainWindow::onLibraryAddGameRequested);
        connect(library, &LibraryScreen::libraryChanged, this, &MainWindow::saveLibraryToConfig);

        Config::Array libROMs = globalCfg.GetArray("UILibrary");
        QSet<QString> known;
        for (int i = 0; i < (int)libROMs.Size(); i++)
        {
            std::string item = libROMs.GetString(i);
            if (!item.empty())
            {
                library->addGame(QString::fromStdString(item));
                known.insert(QString::fromStdString(item));
            }
        }

        // The "games" folder is where installGameToLibrary() copies ROMs
        // to, but until now the home screen only ever showed whatever was
        // already listed in the saved UILibrary array -- files sitting in
        // that folder without a matching config entry (e.g. copied there
        // by hand, or left over from before) were invisible. Pick up
        // anything in there that isn't already known so it actually shows.
        QString gamesDirPath = QString::fromStdString(Platform::GetLocalFilePath("games"));
        QDir gamesDir(gamesDirPath);
        if (gamesDir.exists())
        {
            QStringList filters = {"*.nds", "*.dsi", "*.srl", "*.ids"};
            for (const QFileInfo& fi : gamesDir.entryInfoList(filters, QDir::Files))
            {
                if (!known.contains(fi.absoluteFilePath()))
                    library->addGame(fi.absoluteFilePath());
            }
        }

        setCentralWidget(centralStack);
    }

    if (centralStack->indexOf(panel) < 0)
        centralStack->addWidget(panel);

    centralStack->setCurrentWidget(showingLibrary ? (QWidget*)library : (QWidget*)panel);

#if defined(Q_OS_WIN)
    // ScreenPanelGL uses WA_NativeWindow, so it owns a real HWND instead of
    // being an "alien" raster widget like the library screen's tiles. On
    // Windows, Qt keeps native and alien sibling widgets in the correct
    // z-order via an internal SetWindowPos fixup that runs on the next
    // event loop pass - it isn't guaranteed to have happened yet by the
    // time setCurrentWidget() above returns. Switching stacks right after
    // creating/recreating the panel (e.g. changing renderer) can leave the
    // native HWND behind the previously-shown widget's cached backing
    // store until something forces a repaint, which is what shows up as
    // the leftover library tile "ghosting" over the emu screen. Forcing
    // an explicit hide of the non-current widget plus a deferred repaint
    // clears it reliably; this glitch doesn't happen on Linux/macOS since
    // they don't split native vs alien widgets the same way.
    library->setVisible(showingLibrary);
    panel->setVisible(!showingLibrary);
    QWidget* shownWidget = showingLibrary ? (QWidget*)library : (QWidget*)panel;
    QTimer::singleShot(0, this, [this, shownWidget]()
    {
        if (!centralStack) return;
        shownWidget->raise();
        centralStack->repaint();
    });
#endif

    if (hasMenu)
        actScreenFiltering->setEnabled(hasOGL);
    panel->osdSetEnabled(showOSD);

    connect(emuThread, SIGNAL(windowUpdate()), panel, SLOT(repaint()));

    connect(this, SIGNAL(screenLayoutChange()), panel, SLOT(onScreenLayoutChanged()));
    emit screenLayoutChange();
}

GL::Context* MainWindow::getOGLContext()
{
    if (!hasOGL) return nullptr;

    ScreenPanelGL* glpanel = static_cast<ScreenPanelGL*>(panel);
    return glpanel->getContext();
}

void MainWindow::initOpenGL()
{
    if (!hasOGL) return;

    ScreenPanelGL* glpanel = static_cast<ScreenPanelGL*>(panel);
    return glpanel->initOpenGL();
}

void MainWindow::deinitOpenGL()
{
    if (!hasOGL) return;

    ScreenPanelGL* glpanel = static_cast<ScreenPanelGL*>(panel);
    return glpanel->deinitOpenGL();
}

void MainWindow::setGLSwapInterval(int intv)
{
    if (!hasOGL) return;

    ScreenPanelGL* glpanel = static_cast<ScreenPanelGL*>(panel);
    if (!glpanel) return;
    return glpanel->setSwapInterval(intv);
}

void MainWindow::makeCurrentGL()
{
    if (!hasOGL) return;

    ScreenPanelGL* glpanel = static_cast<ScreenPanelGL*>(panel);
    if (!glpanel) return;
    return glpanel->makeCurrentGL();
}

void MainWindow::releaseGL()
{
    if (!hasOGL) return;

    ScreenPanelGL* glpanel = static_cast<ScreenPanelGL*>(panel);
    if (!glpanel) return;
    return glpanel->releaseGL();
}

void MainWindow::drawScreen()
{
    if (!panel) return;
    return panel->drawScreen();
}

void MainWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->isAutoRepeat()) return;

    if (event->key() == Qt::Key_Escape && emuInstance->emuIsActive())
    {
        togglePauseMenu();
        return;
    }

    // TODO!! REMOVE ME IN RELEASE BUILDS!!
    //if (event->key() == Qt::Key_F11) emuInstance->getNDS()->debug(0);

    if (liveKeyboardPreview && showKeyboardPreview)
    {
        int raw = event->key();
        if (event->modifiers() & Qt::KeypadModifier) raw |= Qt::KeypadModifier;
        liveKeyboardPreview->setKeyState(raw, true);
    }
    if (pausedKeyboardPreview)
    {
        int raw = event->key();
        if (event->modifiers() & Qt::KeypadModifier) raw |= Qt::KeypadModifier;
        pausedKeyboardPreview->setKeyState(raw, true);
    }

    emuInstance->onKeyPress(event);
}

void MainWindow::keyReleaseEvent(QKeyEvent* event)
{
    if (event->isAutoRepeat()) return;

    if (liveKeyboardPreview && showKeyboardPreview)
    {
        int raw = event->key();
        if (event->modifiers() & Qt::KeypadModifier) raw |= Qt::KeypadModifier;
        liveKeyboardPreview->setKeyState(raw, false);
    }
    if (pausedKeyboardPreview)
    {
        int raw = event->key();
        if (event->modifiers() & Qt::KeypadModifier) raw |= Qt::KeypadModifier;
        pausedKeyboardPreview->setKeyState(raw, false);
    }

    emuInstance->onKeyRelease(event);
}


void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (!event->mimeData()->hasUrls()) return;

    QList<QUrl> urls = event->mimeData()->urls();
    if (urls.count() > 1) return; // not handling more than one file at once

    QString filename = urls.at(0).toLocalFile();

    if (FileIsSupportedFiletype(filename))
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    if (!event->mimeData()->hasUrls()) return;

    QList<QUrl> urls = event->mimeData()->urls();
    if (urls.count() > 1) return; // not handling more than one file at once

    if (!verifySetup())
        return;

    const QStringList file = splitArchivePath(urls.at(0).toLocalFile(), false);
    if (file.isEmpty())
        return;

    const QString filename = file.last();
    const bool romInsideArchive = file.size() > 1;
    const auto matchMode = romInsideArchive ? QMimeDatabase::MatchExtension : QMimeDatabase::MatchDefault;
    const QMimeType mimetype = QMimeDatabase().mimeTypeForFile(filename, matchMode);

    bool isNdsRom = NdsRomByExtension(filename) || NdsRomByMimetype(mimetype);
    bool isGbaRom = GbaRomByExtension(filename) || GbaRomByMimetype(mimetype);
    isNdsRom |= ZstdNdsRomByExtension(filename);
    isGbaRom |= ZstdGbaRomByExtension(filename);

    QString errorstr;
    if (isNdsRom)
    {
        if (!emuThread->bootROM(file, errorstr))
        {
            QMessageBox::critical(this, "MelonDS - Ixranium Fork", errorstr);
            return;
        }

        const QString barredFilename = file.join('|');
        recentFileList.removeAll(barredFilename);
        recentFileList.prepend(barredFilename);
        updateRecentFilesMenu();

        updateCartInserted(false);
    }
    else if (isGbaRom)
    {
        if (!emuThread->insertCart(file, true, errorstr))
        {
            QMessageBox::critical(this, "MelonDS - Ixranium Fork", errorstr);
            return;
        }

        updateCartInserted(true);
    }
    else
    {
        QMessageBox::critical(this, "MelonDS - Ixranium Fork", "The file could not be recognized as a DS or GBA ROM.");
        return;
    }
}

void MainWindow::focusInEvent(QFocusEvent* event)
{
    onFocusIn();
}

void MainWindow::focusOutEvent(QFocusEvent* event)
{
    onFocusOut();
}

void MainWindow::onFocusIn()
{
    focused = true;
    if (emuInstance)
        emuInstance->updateAudioMuteByWindowFocus();
}

void MainWindow::onFocusOut()
{
    // focusOutEvent is called through the window close event handler
    // prevent use after free
    focused = false;
    if (emuInstance)
        emuInstance->updateAudioMuteByWindowFocus();
}

void MainWindow::onAppStateChanged(Qt::ApplicationState state)
{
    if (state == Qt::ApplicationInactive)
    {
        emuInstance->keyReleaseAll();
        if (pauseOnLostFocus && emuThread->emuIsRunning())
            emuThread->emuPause();
    }
    else if (state == Qt::ApplicationActive)
    {
        if (pauseOnLostFocus && !pausedManually)
            emuThread->emuUnpause();
    }
}

bool MainWindow::verifySetup()
{
    QString res = emuInstance->verifySetup();
    if (!res.isEmpty())
    {
         QMessageBox::critical(this, "MelonDS - Ixranium Fork", res);
         return false;
    }

    return true;
}

bool MainWindow::preloadROMs(QStringList file, QStringList gbafile, bool boot)
{
    QString errorstr;

    if (file.isEmpty() && gbafile.isEmpty() && !boot)
        return false;

    if (!verifySetup())
    {
        return false;
    }

    bool gbaloaded = false;
    if (!gbafile.isEmpty())
    {
        if (!emuThread->insertCart(gbafile, true, errorstr))
        {
            QMessageBox::critical(this, "MelonDS - Ixranium Fork", errorstr);
            return false;
        }

        gbaloaded = true;
    }

    bool ndsloaded = false;
    if (!file.isEmpty())
    {
        if (boot)
        {
            if (!emuThread->bootROM(file, errorstr))
            {
                QMessageBox::critical(this, "MelonDS - Ixranium Fork", errorstr);
                return false;
            }
        }
        else
        {
            if (!emuThread->insertCart(file, false, errorstr))
            {
                QMessageBox::critical(this, "MelonDS - Ixranium Fork", errorstr);
                return false;
            }
        }

        recentFileList.removeAll(file.join("|"));
        recentFileList.prepend(file.join("|"));
        updateRecentFilesMenu();
        ndsloaded = true;
    }
    else if (boot)
    {
        if (!emuThread->bootFirmware(errorstr))
        {
            QMessageBox::critical(this, "MelonDS - Ixranium Fork", errorstr);
            return false;
        }
    }

    updateCartInserted(false);
    if (gbaloaded)
        updateCartInserted(true);

    return true;
}

QStringList MainWindow::splitArchivePath(const QString& filename, bool useMemberSyntax)
{
    if (filename.isEmpty()) return {};

#ifdef ARCHIVE_SUPPORT_ENABLED
    if (useMemberSyntax)
    {
        const QStringList filenameParts = filename.split('|');
        if (filenameParts.size() > 2)
        {
            QMessageBox::warning(this, "MelonDS - Ixranium Fork", "This path contains too many '|'.");
            return {};
        }

        if (filenameParts.size() == 2)
        {
            const QString archive = filenameParts.at(0);
            if (!QFileInfo(archive).exists())
            {
                QMessageBox::warning(this, "MelonDS - Ixranium Fork", "This archive does not exist.");
                return {};
            }

            const QString subfile = filenameParts.at(1);
            if (!Archive::ListArchive(archive).contains(subfile))
            {
                QMessageBox::warning(this, "MelonDS - Ixranium Fork", "This archive does not contain the desired file.");
                return {};
            }

            return filenameParts;
        }
    }
#endif

    if (!QFileInfo(filename).exists())
    {
        QMessageBox::warning(this, "MelonDS - Ixranium Fork", "This ROM file does not exist.");
        return {};
    }

#ifdef ARCHIVE_SUPPORT_ENABLED
    if (SupportedArchiveByExtension(filename)
        || SupportedArchiveByMimetype(QMimeDatabase().mimeTypeForFile(filename)))
    {
        const QString subfile = pickFileFromArchive(filename);
        if (subfile.isEmpty())
            return {};

        return { filename, subfile };
    }
#endif

    return { filename };
}

QString MainWindow::pickFileFromArchive(QString archiveFileName)
{
    QVector<QString> archiveROMList = Archive::ListArchive(archiveFileName);

    if (archiveROMList.size() <= 1)
    {
        if (!archiveROMList.isEmpty() && archiveROMList.at(0) == "OK")
            QMessageBox::warning(this, "MelonDS - Ixranium Fork", "This archive is empty.");
        else
            QMessageBox::critical(this, "MelonDS - Ixranium Fork", "This archive could not be read. It may be corrupt or you don't have the permissions.");
        return QString();
    }

    archiveROMList.removeFirst();

    const auto notSupportedRom = [&](const auto& filename){
        if (NdsRomByExtension(filename) || GbaRomByExtension(filename))
            return false;
        const QMimeType mimetype = QMimeDatabase().mimeTypeForFile(filename, QMimeDatabase::MatchExtension);
        return !(NdsRomByMimetype(mimetype) || GbaRomByMimetype(mimetype));
    };

    archiveROMList.erase(std::remove_if(archiveROMList.begin(), archiveROMList.end(), notSupportedRom),
                         archiveROMList.end());

    if (archiveROMList.isEmpty())
    {
        QMessageBox::warning(this, "MelonDS - Ixranium Fork", "This archive does not contain any supported ROMs.");
        return QString();
    }

    if (archiveROMList.size() == 1)
        return archiveROMList.first();

    bool ok;
    const QString toLoad = QInputDialog::getItem(
        this, "MelonDS - Ixranium Fork",
        "This archive contains multiple files. Select which ROM you want to load.",
        archiveROMList.toList(), 0, false, &ok
    );

    if (ok) return toLoad;

    // User clicked on cancel

    return QString();
}

QStringList MainWindow::pickROM(bool gba)
{
    emuThread->emuPause();

    const QString console = gba ? "GBA" : "DS";
    const QStringList& romexts = gba ? GbaRomExtensions : NdsRomExtensions;

    QString rawROMs = romexts.join(" *");
    QString extraFilters = ";;" + console + " ROMs (*" + rawROMs;
    QString allROMs = rawROMs;

    QString zstdROMs = "*" + romexts.join(".zst *") + ".zst";
    extraFilters += ");;Zstandard-compressed " + console + " ROMs (" + zstdROMs + ")";
    allROMs += " " + zstdROMs;

#ifdef ARCHIVE_SUPPORT_ENABLED
    QString archives = "*" + ArchiveExtensions.join(" *");
    extraFilters += ";;Archives (" + archives + ")";
    allROMs += " " + archives;
#endif
    extraFilters += ";;All files (*.*)";

    const QString filename = QFileDialog::getOpenFileName(
        this, "Open " + console + " ROM",
        globalCfg.GetQString("LastROMFolder"),
        "All supported files (*" + allROMs + ")" + extraFilters
    );

    if (filename.isEmpty())
    {
        emuThread->emuUnpause();
        return {};
    }

    globalCfg.SetQString("LastROMFolder", QFileInfo(filename).dir().path());
    auto ret = splitArchivePath(filename, false);
    emuThread->emuUnpause();
    return ret;
}

void MainWindow::updateCartInserted(bool gba)
{
    bool inserted;
    QString label;
    if (gba)
    {
        inserted = emuInstance->gbaCartInserted() && (emuInstance->getConsoleType() == 0);
        label = tr("GBA slot: ") + emuInstance->gbaCartLabel();

        emuInstance->doOnAllWindows([=](MainWindow* win)
        {
            if (!win->hasMenu) return;
            win->actCurrentGBACart->setText(label);
            win->actEjectGBACart->setEnabled(inserted);
        });
    }
    else
    {
        inserted = emuInstance->cartInserted();
        label = tr("DS slot: ") + emuInstance->cartLabel();

        emuInstance->doOnAllWindows([=](MainWindow* win)
        {
            if (!win->hasMenu) return;
            win->actCurrentCart->setText(label);
            win->actEjectCart->setEnabled(inserted);
            win->actImportSavefile->setEnabled(inserted);
            win->actEnableCheats->setEnabled(inserted);
            win->actSetupCheats->setEnabled(inserted);
            win->actROMInfo->setEnabled(inserted);
            win->actRAMInfo->setEnabled(inserted);
        });
    }
}

void MainWindow::onLibraryAddGameRequested()
{
    if (!verifySetup())
        return;

    QStringList file = pickROM(false);
    if (file.isEmpty())
        return;

    QString installedPath = installGameToLibrary(file);
    QString path = !installedPath.isEmpty() ? installedPath : file.join('|');

    library->addGame(path);

    saveLibraryToConfig();

    onLibraryGameActivated(path);
}

QString MainWindow::installGameToLibrary(const QStringList& file)
{
    // Archive entries ("archive.zip|game.nds") stay where they are; we can't
    // meaningfully "install" a ROM that lives inside an archive.
    if (file.size() != 1)
        return QString();

    QString srcPath = file.first();
    QFileInfo srcInfo(srcPath);

    QString gamesDirPath = QString::fromStdString(Platform::GetLocalFilePath("games"));
    QDir gamesDir(gamesDirPath);
    if (!gamesDir.exists() && !gamesDir.mkpath("."))
        return QString();

    // Already installed (e.g. re-adding a game already in the games folder).
    if (QDir::cleanPath(srcInfo.absoluteFilePath()) == QDir::cleanPath(gamesDir.filePath(srcInfo.fileName())))
        return srcInfo.absoluteFilePath();

    QString destPath = gamesDir.filePath(srcInfo.fileName());
    if (QFile::exists(destPath))
    {
        // A file with this name is already installed. That's only safe to
        // reuse if it's actually the SAME game -- two unrelated ROMs (very
        // common with homebrew/fan-translation files, which often share
        // generic names like "game.nds") can easily collide on filename
        // alone. Compare by size first (cheap), then by content hash, and
        // only treat it as "already installed" if the bytes actually match.
        // Otherwise, install alongside it under a disambiguated name.
        QFileInfo destInfo(destPath);
        bool sameFile = false;
        if (destInfo.size() == srcInfo.size())
        {
            QFile srcFile(srcPath);
            QFile destFile(destPath);
            if (srcFile.open(QIODevice::ReadOnly) && destFile.open(QIODevice::ReadOnly))
            {
                QCryptographicHash srcHash(QCryptographicHash::Sha256);
                QCryptographicHash destHash(QCryptographicHash::Sha256);
                srcHash.addData(&srcFile);
                destHash.addData(&destFile);
                sameFile = (srcHash.result() == destHash.result());
            }
        }

        if (sameFile)
            return destPath;

        // Different game, same filename: find a free "name (2).ext",
        // "name (3).ext", etc. instead of silently reusing the wrong file.
        QString base = srcInfo.completeBaseName();
        QString ext = srcInfo.suffix();
        int counter = 2;
        do
        {
            QString candidate = ext.isEmpty() ? QString("%1 (%2)").arg(base).arg(counter)
                                               : QString("%1 (%2).%3").arg(base).arg(counter).arg(ext);
            destPath = gamesDir.filePath(candidate);
            counter++;
        } while (QFile::exists(destPath));
    }

    if (!QFile::copy(srcPath, destPath))
        return QString();

    if (globalCfg.GetBool("Library.DesktopShortcuts"))
        createDesktopShortcut(srcInfo.completeBaseName(), destPath);

    bool suppressPrompt = globalCfg.GetBool("Library.SuppressDeletePrompt");
    if (!suppressPrompt)
    {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle("Game installed");
        box.setText("\"" + srcInfo.fileName() + "\" has been installed to your library.\n\n"
                     "Delete the original file to save disk space?");
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box.setDefaultButton(QMessageBox::No);

        QCheckBox* dontAskAgain = new QCheckBox("Don't show this message again");
        box.setCheckBox(dontAskAgain);

        int result = box.exec();

        if (dontAskAgain->isChecked())
            globalCfg.SetBool("Library.SuppressDeletePrompt", true);

        if (result == QMessageBox::Yes)
            QFile::remove(srcPath);
    }

    return destPath;
}

QString MainWindow::detectDesktopPath()
{
#if defined(Q_OS_WIN)
    // SHGetKnownFolderPath resolves the *actual* current Desktop location
    // straight from the registry/shell -- including when it's been moved
    // or redirected (e.g. by OneDrive folder backup), which a hardcoded
    // "%USERPROFILE%\Desktop" guess would miss. Also avoids spawning a
    // powershell.exe process just to read one path, which could exceed
    // the old 3s timeout on a slow first launch and silently fail.
    // SHGetKnownFolderPath needs COM initialized on the calling thread to
    // reliably resolve redirected folders (e.g. OneDrive-managed Desktop).
    // Without this, it can silently fail here and fall through to the
    // QStandardPaths fallback below, which does NOT know about OneDrive
    // redirection and returns the wrong (default) Desktop path.
    HRESULT coHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    bool needCoUninit = SUCCEEDED(coHr); // see RPC_E_CHANGED_MODE note elsewhere in this file

    PWSTR pathPtr = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &pathPtr)) && pathPtr)
    {
        QString path = QString::fromWCharArray(pathPtr);
        CoTaskMemFree(pathPtr);
        if (needCoUninit)
            CoUninitialize();
        if (!path.isEmpty())
            return path;
    }
    if (needCoUninit)
        CoUninitialize();
#elif defined(Q_OS_MAC)
    QString path = QDir::homePath() + "/Desktop";
    if (QDir(path).exists())
        return path;
#else // Linux and other Unix-likes
    QProcess proc;
    proc.start("xdg-user-dir", {"DESKTOP"});
    proc.waitForFinished(3000);
    QString path = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
    if (!path.isEmpty() && QDir(path).exists())
        return path;

    path = QDir::homePath() + "/Desktop";
    if (QDir(path).exists())
        return path;
#endif

    // Fallback for any platform where the above didn't work out.
    QString fallback = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!fallback.isEmpty())
        return fallback;

    return QDir::homePath() + "/Desktop";
}

void MainWindow::createDesktopShortcut(const QString& gameName, const QString& gamePath)
{
    QString desktopPath = detectDesktopPath();
    if (desktopPath.isEmpty())
        return;

    QDir desktopDir(desktopPath);
    if (!desktopDir.exists())
        return;

    QString safeName = gameName;
    safeName.replace(QRegularExpression("[\\\\/:*?\"<>|]"), "_");
    if (safeName.isEmpty())
        safeName = "MelonDS - Ixranium Fork Game";

    QString exePath = QCoreApplication::applicationFilePath();
    exePath = QDir::toNativeSeparators(exePath);
    QString nativeGamePath = QDir::toNativeSeparators(gamePath);

    // Prefer the game's own icon (decoded from the NDS ROM banner); fall
    // back to melonDS's own app icon if the ROM has none (e.g. homebrew).
    QImage iconImg = LibraryScreen::loadRomIconImage(gamePath);
    if (iconImg.isNull())
        iconImg = QIcon(":/melon-icon").pixmap(256, 256).toImage();

    QString iconsDirPath = QString::fromStdString(Platform::GetLocalFilePath("shortcut_icons"));
    QDir iconsDir(iconsDirPath);
    if (!iconsDir.exists())
        iconsDir.mkpath(".");

#if defined(Q_OS_WIN)
    QString shortcutPath = QDir::toNativeSeparators(desktopDir.filePath(safeName + ".lnk"));

    QString iconLocation = exePath;
    int iconIndex = 0;
    if (!iconImg.isNull())
    {
        QImage scaled = iconImg.scaled(256, 256, Qt::KeepAspectRatio, Qt::FastTransformation);
        QString icoPath = iconsDir.filePath(safeName + ".ico");
        if (scaled.save(icoPath, "ICO"))
            iconLocation = QDir::toNativeSeparators(icoPath);
    }

    // Previously this shelled out to a PowerShell one-liner to build the
    // .lnk via WScript.Shell. That was fragile in practice: manual
    // single-quote escaping of paths could go wrong for names with quotes
    // or backslash sequences, PowerShell's execution policy or AV/EDR
    // software could silently block a script launched from another app,
    // and QProcess::execute() blocked the UI thread waiting on a whole
    // interpreter to spin up. Using the shell's native IShellLink/
    // IPersistFile COM objects directly avoids all of that -- no
    // subprocess, no quoting, no policy to trip over.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    // RPC_E_CHANGED_MODE means COM was already initialized on this thread in
    // a DIFFERENT concurrency mode by someone else (e.g. Qt's own Windows
    // platform plugin, or another library) -- this call didn't actually
    // initialize anything and doesn't own a reference to release. Calling
    // CoUninitialize() in that case would tear down COM out from under
    // whichever code already had it initialized, causing unrelated,
    // hard-to-diagnose failures elsewhere (drag&drop, native file dialogs,
    // WASAPI audio, etc). Only SUCCEEDED(hr) (S_OK or S_FALSE) means this
    // call actually holds a reference that needs to be released.
    bool needUninit = SUCCEEDED(hr);

    IShellLinkW* shellLink = nullptr;
    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER,
                           IID_IShellLinkW, reinterpret_cast<void**>(&shellLink));
    if (SUCCEEDED(hr) && shellLink)
    {
        shellLink->SetPath(reinterpret_cast<const wchar_t*>(exePath.utf16()));
        shellLink->SetArguments(reinterpret_cast<const wchar_t*>(
            (QString("\"") + nativeGamePath + "\"").utf16()));
        shellLink->SetWorkingDirectory(reinterpret_cast<const wchar_t*>(
            QDir::toNativeSeparators(QCoreApplication::applicationDirPath()).utf16()));
        shellLink->SetIconLocation(reinterpret_cast<const wchar_t*>(iconLocation.utf16()), iconIndex);

        IPersistFile* persistFile = nullptr;
        hr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&persistFile));
        if (SUCCEEDED(hr) && persistFile)
        {
            persistFile->Save(reinterpret_cast<const wchar_t*>(shortcutPath.utf16()), TRUE);
            persistFile->Release();
        }
        shellLink->Release();
    }

    if (needUninit)
        CoUninitialize();

#elif defined(Q_OS_MAC)
    // Plain .command scripts can't carry a custom icon on macOS - only a
    // real .app bundle can. Build a minimal one: a launcher script plus an
    // .icns built from the ROM icon via the system's own sips/iconutil.
    QString bundlePath = desktopDir.filePath(safeName + ".app");

    QDir bundleDir(bundlePath);
    if (bundleDir.exists())
    {
        // Replacing an existing shortcut - clear it out first.
        bundleDir.removeRecursively();
    }

    QDir().mkpath(bundlePath + "/Contents/MacOS");
    QDir().mkpath(bundlePath + "/Contents/Resources");

    QFile runScript(bundlePath + "/Contents/MacOS/run");
    if (runScript.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream out(&runScript);
        out << "#!/bin/bash\n";
        out << "\"" << exePath << "\" \"" << nativeGamePath << "\"\n";
        runScript.close();
        runScript.setPermissions(runScript.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    }

    bool haveIcon = false;
    if (!iconImg.isNull())
    {
        QString iconsetPath = iconsDir.filePath(safeName + ".iconset");
        QDir(iconsetPath).removeRecursively();
        QDir().mkpath(iconsetPath);

        struct { int size; const char* name; } sizes[] = {
            {16, "icon_16x16.png"},   {32, "icon_16x16@2x.png"},
            {32, "icon_32x32.png"},   {64, "icon_32x32@2x.png"},
            {128, "icon_128x128.png"}, {256, "icon_128x128@2x.png"},
            {256, "icon_256x256.png"}, {512, "icon_256x256@2x.png"},
            {512, "icon_512x512.png"}, {1024, "icon_512x512@2x.png"},
        };

        bool wroteAny = false;
        for (const auto& s : sizes)
        {
            QImage scaled = iconImg.scaled(s.size, s.size, Qt::KeepAspectRatio, Qt::FastTransformation);
            if (scaled.save(iconsetPath + "/" + s.name, "PNG"))
                wroteAny = true;
        }

        if (wroteAny)
        {
            QString icnsPath = bundlePath + "/Contents/Resources/icon.icns";
            QProcess::execute("iconutil", {"-c", "icns", iconsetPath, "-o", icnsPath});
            haveIcon = QFile::exists(icnsPath);
        }

        QDir(iconsetPath).removeRecursively();
    }

    QFile plist(bundlePath + "/Contents/Info.plist");
    if (plist.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream out(&plist);
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
        out << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n";
        out << "<plist version=\"1.0\">\n<dict>\n";
        out << "  <key>CFBundleExecutable</key><string>run</string>\n";
        out << "  <key>CFBundleName</key><string>" << gameName << "</string>\n";
        out << "  <key>CFBundlePackageType</key><string>APPL</string>\n";
        if (haveIcon)
            out << "  <key>CFBundleIconFile</key><string>icon.icns</string>\n";
        out << "</dict>\n</plist>\n";
        plist.close();
    }

    // Let Finder know the bundle's metadata changed so it picks up the icon.
    QProcess::execute("touch", {bundlePath});

#else // Linux and other Unix-likes
    QString shortcutPath = desktopDir.filePath(safeName + ".desktop");

    QString iconPath = exePath;
    if (!iconImg.isNull())
    {
        QImage scaled = iconImg.scaled(256, 256, Qt::KeepAspectRatio, Qt::FastTransformation);
        QString pngPath = iconsDir.filePath(safeName + ".png");
        if (scaled.save(pngPath, "PNG"))
            iconPath = pngPath;
    }

    QFile file(shortcutPath);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        QTextStream out(&file);
        out << "[Desktop Entry]\n";
        out << "Type=Application\n";
        out << "Name=" << gameName << "\n";
        out << "Exec=\"" << exePath << "\" \"" << nativeGamePath << "\"\n";
        out << "Icon=" << iconPath << "\n";
        out << "Terminal=false\n";
        out << "Categories=Game;\n";
        file.close();
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeGroup | QFileDevice::ExeOther);
    }
#endif
}

void MainWindow::saveLibraryToConfig()
{
    Config::Array libROMs = globalCfg.GetArray("UILibrary");
    libROMs.Clear();
    QStringList all = library->gamePaths();
    for (int i = 0; i < all.size(); i++)
        libROMs.SetQString(i, all.at(i));
    Config::Save();
}

void MainWindow::onLibraryGameActivated(QString path)
{
    if (!verifySetup())
        return;

    QStringList file = path.split('|');

    // Per-game "Details" console type override (set via the library tile's
    // right-click menu): temporarily swap the global Console type setting
    // for just this boot, then restore whatever the user has as their
    // actual global default so this never overwrites it permanently.
    int overrideConsole = library ? library->consoleTypeOverride(path) : -1;
    int savedConsoleType = globalCfg.GetInt("Emu.ConsoleType");
    if (overrideConsole == 0 || overrideConsole == 1)
        globalCfg.SetInt("Emu.ConsoleType", overrideConsole);

    QString errorstr;
    bool ok = emuThread->bootROM(file, errorstr);

    if (overrideConsole == 0 || overrideConsole == 1)
        globalCfg.SetInt("Emu.ConsoleType", savedConsoleType);

    if (!ok)
    {
        QMessageBox::critical(this, "MelonDS - Ixranium Fork", errorstr);
        return;
    }

    // Per-game "Details" control scheme override: reload the global
    // keyboard mapping first so a previous game's override (if any) can't
    // leak into this boot, then layer this game's override on top if it
    // has one. Applied to the live mapping only (never written to config),
    // so it's purely in-memory for this session and never touches the
    // user's global keyboard settings.
    emuInstance->inputLoadConfig();
    QString overrideScheme = library ? library->controlSchemeOverride(path) : QString();
    if (!overrideScheme.isEmpty())
    {
        int nativeMap[12];
        if (ControlSchemeStore::load(overrideScheme, nativeMap))
            emuInstance->applyKeypadKeyOverride(nativeMap);
    }
    refreshKeyboardPreviews();

    recentFileList.removeAll(path);
    recentFileList.prepend(path);
    updateRecentFilesMenu();

    updateCartInserted(false);
}

void MainWindow::onOpenFile()
{
    if (!verifySetup())
        return;

    QStringList file = pickROM(false);
    if (file.isEmpty())
        return;

    QString errorstr;
    if (!emuThread->bootROM(file, errorstr))
    {
        QMessageBox::critical(this, "MelonDS - Ixranium Fork", errorstr);
        return;
    }

    QString filename = file.join('|');
    recentFileList.removeAll(filename);
    recentFileList.prepend(filename);
    updateRecentFilesMenu();

    updateCartInserted(false);
}

void MainWindow::onClearRecentFiles()
{
    recentFileList.clear();
    globalCfg.GetArray("RecentROM").Clear();
    updateRecentFilesMenu();
}

void MainWindow::loadRecentFilesMenu(bool loadcfg)
{
    if (loadcfg)
    {
        recentFileList.clear();

        Config::Array recentROMs = globalCfg.GetArray("RecentROM");
        int numrecent = std::min(kMaxRecentROMs, (int) recentROMs.Size());
        for (int i = 0; i < numrecent; ++i)
        {
            std::string item = recentROMs.GetString(i);
            if (!item.empty())
                recentFileList.push_back(QString::fromStdString(item));
        }
    }

    recentMenu->clear();

    for (int i = 0; i < recentFileList.size(); ++i)
    {
        if (i >= kMaxRecentROMs) break;

        QString item_full = recentFileList.at(i);
        QString item_display = item_full;
        int itemlen = item_full.length();
        const int maxlen = 100;
        if (itemlen > maxlen)
        {
            int cut_start = 0;
            while (item_full[cut_start] != '/' && item_full[cut_start] != '\\' &&
                   cut_start < itemlen)
                cut_start++;

            int cut_end = itemlen-1;
            while (((item_full[cut_end] != '/' && item_full[cut_end] != '\\') ||
                    (cut_start+4+(itemlen-cut_end) < maxlen)) &&
                   cut_end > 0)
                cut_end--;

            item_display.truncate(cut_start+1);
            item_display += "...";
            item_display += QString(item_full).remove(0, cut_end);
        }

        QAction *actRecentFile_i = recentMenu->addAction(QString("%1.  %2").arg(i+1).arg(item_display));
        actRecentFile_i->setData(item_full);
        connect(actRecentFile_i, &QAction::triggered, this, &MainWindow::onClickRecentFile);
    }

    while (recentFileList.size() > 10)
        recentFileList.removeLast();

    recentMenu->addSeparator();

    QAction *actClearRecentList = recentMenu->addAction(tr("Clear"));
    connect(actClearRecentList, &QAction::triggered, this, &MainWindow::onClearRecentFiles);

    if (recentFileList.empty())
        actClearRecentList->setEnabled(false);
}

void MainWindow::updateRecentFilesMenu()
{
    Config::Array recentroms = globalCfg.GetArray("RecentROM");
    recentroms.Clear();

    for (int i = 0; i < recentFileList.size(); ++i)
    {
        if (i >= kMaxRecentROMs) break;

        recentroms.SetQString(i, recentFileList.at(i));
    }

    Config::Save();
    loadRecentFilesMenu(false);

    emuInstance->broadcastCommand(InstCmd_UpdateRecentFiles);
}

void MainWindow::onClickRecentFile()
{
    QAction *act = (QAction *)sender();
    QString filename = act->data().toString();

    if (!verifySetup())
        return;

    const QStringList file = splitArchivePath(filename, true);
    if (file.isEmpty())
        return;

    QString errorstr;
    if (!emuThread->bootROM(file, errorstr))
    {
        QMessageBox::critical(this, "MelonDS - Ixranium Fork", errorstr);
        return;
    }

    recentFileList.removeAll(filename);
    recentFileList.prepend(filename);
    updateRecentFilesMenu();

    updateCartInserted(false);
}

void MainWindow::onBootFirmware()
{
    if (!verifySetup())
        return;

    QString errorstr;
    if (!emuThread->bootFirmware(errorstr))
    {
        QMessageBox::critical(this, "MelonDS - Ixranium Fork", errorstr);
        return;
    }
}

void MainWindow::onInsertCart()
{
    QStringList file = pickROM(false);
    if (file.isEmpty())
        return;

    QString errorstr;
    if (!emuThread->insertCart(file, false, errorstr))
    {
        QMessageBox::critical(this, "MelonDS - Ixranium Fork", errorstr);
        return;
    }

    updateCartInserted(false);
}

void MainWindow::onEjectCart()
{
    emuThread->ejectCart(false);
    updateCartInserted(false);
}

void MainWindow::onInsertGBACart()
{
    QStringList file = pickROM(true);
    if (file.isEmpty())
        return;

    QString errorstr;
    if (!emuThread->insertCart(file, true, errorstr))
    {
        QMessageBox::critical(this, "MelonDS - Ixranium Fork", errorstr);
        return;
    }

    updateCartInserted(true);
}

void MainWindow::onInsertGBAAddon()
{
    QAction* act = (QAction*)sender();
    int type = act->data().toInt();

    QString errorstr;
    if (!emuThread->insertGBAAddon(type, errorstr))
    {
        QMessageBox::critical(this, "MelonDS - Ixranium Fork", errorstr);
        return;
    }

    updateCartInserted(true);
}

void MainWindow::onEjectGBACart()
{
    emuThread->ejectCart(true);
    updateCartInserted(true);
}

void MainWindow::onSaveState()
{
    int slot = ((QAction*)sender())->data().toInt();

    QString filename;
    if (slot > 0)
    {
        filename = QString::fromStdString(emuInstance->getSavestateName(slot));
    }
    else
    {
        // TODO: specific 'last directory' for savestate files?
        emuThread->emuPause();
        filename = QFileDialog::getSaveFileName(this,
                                                         "Save state",
                                                         globalCfg.GetQString("LastROMFolder"),
                                                         "MelonDS - Ixranium Fork savestates (*.mln);;Any file (*.*)");
        emuThread->emuUnpause();
        if (filename.isEmpty())
            return;
    }

    if (emuThread->saveState(filename))
    {
        if (slot > 0) emuInstance->osdAddMessage(0, "State saved to slot %d", slot);
        else          emuInstance->osdAddMessage(0, "State saved to file");

        actLoadState[slot]->setEnabled(true);
    }
    else
    {
        emuInstance->osdAddMessage(0xFFA0A0, "State save failed");
    }
}

void MainWindow::onLoadState()
{
    int slot = ((QAction*)sender())->data().toInt();

    QString filename;
    if (slot > 0)
    {
        filename = QString::fromStdString(emuInstance->getSavestateName(slot));
    }
    else
    {
        // TODO: specific 'last directory' for savestate files?
        emuThread->emuPause();
        filename = QFileDialog::getOpenFileName(this,
                                                         "Load state",
                                                         globalCfg.GetQString("LastROMFolder"),
                                                         "MelonDS - Ixranium Fork savestates (*.ml*);;Any file (*.*)");
        emuThread->emuUnpause();
        if (filename.isEmpty())
            return;
    }

    if (!Platform::FileExists(filename.toStdString()))
    {
        if (slot > 0) emuInstance->osdAddMessage(0xFFA0A0, "State slot %d is empty", slot);
        else          emuInstance->osdAddMessage(0xFFA0A0, "State file does not exist");

        return;
    }

    if (emuThread->loadState(filename))
    {
        if (slot > 0) emuInstance->osdAddMessage(0, "State loaded from slot %d", slot);
        else          emuInstance->osdAddMessage(0, "State loaded from file");

        actUndoStateLoad->setEnabled(true);
    }
    else
    {
        emuInstance->osdAddMessage(0xFFA0A0, "State load failed");
    }
}

void MainWindow::onUndoStateLoad()
{
    emuThread->undoStateLoad();

    emuInstance->osdAddMessage(0, "State load undone");
}

void MainWindow::onImportSavefile()
{
    QString path = QFileDialog::getOpenFileName(this,
                                            "Select savefile",
                                            globalCfg.GetQString("LastROMFolder"),
                                            "Savefiles (*.sav *.bin *.dsv);;Any file (*.*)");

    if (path.isEmpty())
        return;

    if (!Platform::FileExists(path.toStdString()))
    {
        QMessageBox::critical(this, "MelonDS - Ixranium Fork", "Could not open the given savefile.");
        return;
    }

    if (emuThread->emuIsActive())
    {
        if (QMessageBox::warning(this,
                        "MelonDS - Ixranium Fork",
                        "The emulation will be reset and the current savefile overwritten.",
                        QMessageBox::Ok, QMessageBox::Cancel) != QMessageBox::Ok)
        {
            return;
        }
    }

    if (!emuThread->importSavefile(path))
    {
        QMessageBox::critical(this, "MelonDS - Ixranium Fork", "Could not import the given savefile.");
        return;
    }
}

void MainWindow::onQuit()
{
    close();
}


void MainWindow::onPause(bool checked)
{
    if (!emuThread->emuIsActive()) return;

    if (checked)
    {
        emuThread->emuPause();
        pausedManually = true;
    }
    else
    {
        emuThread->emuUnpause();
        pausedManually = false;
    }
}

void MainWindow::onReset()
{
    if (!emuThread->emuIsActive()) return;

    emuThread->emuReset();
}

void MainWindow::onStop()
{
    if (!emuThread->emuIsActive()) return;

    emuThread->emuStop(true);
}

void MainWindow::onFrameStep()
{
    if (!emuThread->emuIsActive()) return;

    emuThread->emuFrameStep();
}

void MainWindow::onOpenDateTime()
{
    DateTimeDialog* dlg = DateTimeDialog::openDlg(this);
    connect(dlg, &DateTimeDialog::finished, this, &MainWindow::onDateTimeDialogFinished);
}

void MainWindow::onDateTimeDialogFinished(int res)
{
    if (!res) return;
    if (!emuThread->emuIsActive()) return;

    emuInstance->setDateTime();
}

void MainWindow::onOpenPowerManagement()
{
    PowerManagementDialog* dlg = PowerManagementDialog::openDlg(this);
}

void MainWindow::onEnableCheats(bool checked)
{
    localCfg.SetBool("EnableCheats", checked);
    emuThread->enableCheats(checked);

    emuInstance->doOnAllWindows([=](MainWindow* win)
    {
        win->actEnableCheats->setChecked(checked);
    }, windowID);
}

void MainWindow::onSetupCheats()
{
    emuThread->emuPause();

    CheatsDialog* dlg = CheatsDialog::openDlg(this);
    connect(dlg, &CheatsDialog::finished, this, &MainWindow::onCheatsDialogFinished);
}

void MainWindow::onCheatsDialogFinished(int res)
{
    emuThread->emuUnpause();
}

void MainWindow::onROMInfo()
{
    auto cart = emuInstance->nds->NDSCartSlot.GetCart();
    if (cart)
        ROMInfoDialog* dlg = ROMInfoDialog::openDlg(this);
}

void MainWindow::onRAMInfo()
{
    RAMInfoDialog* dlg = RAMInfoDialog::openDlg(this);
}

void MainWindow::onOpenTitleManager()
{
    TitleManagerDialog* dlg = TitleManagerDialog::openDlg(this);
}

void MainWindow::onMPNewInstance()
{
    createEmuInstance();
}

void MainWindow::onLANStartHost()
{
    if (!lanWarning(true)) return;
    LANStartHostDialog::openDlg(this);
}

void MainWindow::onLANStartClient()
{
    if (!lanWarning(false)) return;
    LANStartClientDialog::openDlg(this);
}

void MainWindow::onNPStartHost()
{
    //Netplay::StartHost();
    //NetplayStartHostDialog::openDlg(this);
}

void MainWindow::onNPStartClient()
{
    //Netplay::StartClient();
    //NetplayStartClientDialog::openDlg(this);
}

void MainWindow::onNPTest()
{
    // HAX
    //Netplay::StartGame();
}

void MainWindow::updateMPInterface(MPInterfaceType type)
{
    if (!hasMenu) return;

    // MP interface was changed, reflect it in the UI

    bool enable = (type == MPInterface_Local);
    actMPNewInstance->setEnabled(enable);
    actLANStartHost->setEnabled(enable);
    actLANStartClient->setEnabled(enable);
    /*actNPStartHost->setEnabled(enable);
    actNPStartClient->setEnabled(enable);
    actNPTest->setEnabled(enable);*/
}

bool MainWindow::lanWarning(bool host)
{
    if (numEmuInstances() < 2)
        return true;

    QString verb = host ? "host" : "join";
    QString msg = "Multiple emulator instances are currently open.\n"
            "If you "+verb+" a LAN game now, all secondary instances will be closed.\n\n"
            "Do you wish to continue?";

    auto res = QMessageBox::warning(this, "MelonDS - Ixranium Fork", msg, QMessageBox::Yes|QMessageBox::No, QMessageBox::No);
    if (res == QMessageBox::No)
        return false;

    deleteAllEmuInstances(1);
    return true;
}

void MainWindow::onOpenSettingsHub()
{
    settingsHub = new SettingsHubDialog(this);
    settingsHub->setAttribute(Qt::WA_DeleteOnClose);
    connect(settingsHub, &QObject::destroyed, this, [this]() { settingsHub = nullptr; });

    settingsHub->addCategory(tr("Emu settings"));
    settingsHub->addCategory(tr("Input and hotkeys"));
    settingsHub->addCategory(tr("Video settings"));
    settingsHub->addCategory(tr("Camera settings"));
    settingsHub->addCategory(tr("Audio settings"));
    settingsHub->addCategory(tr("Multiplayer settings"));
    settingsHub->addCategory(tr("Wifi settings"));
    settingsHub->addCategory(tr("Firmware settings"));
    settingsHub->addCategory(tr("Interface settings"));
    settingsHub->addCategory(tr("Path settings"));
    settingsHub->addCategory(tr("Debug settings"));

    connect(settingsHub, &SettingsHubDialog::categorySelected, this, &MainWindow::onSettingsHubCategory);

    settingsHub->open();
}

void MainWindow::onSettingsHubCategory(int index)
{
    if (!settingsHub)
        return;

    // IMPORTANT: these are freshly constructed with `this` (MainWindow) as
    // the parent, exactly like the standalone menu actions do, so the
    // dialogs' internal ((MainWindow*)...)->getEmuInstance() logic works.
    // They must NOT be shown/opened here - setPage() embeds them as plain
    // widgets, and stripping window flags off an already-shown modal dialog
    // is what caused the freeze before.
    switch (index)
    {
        case 0: settingsHub->setPage(new EmuSettingsDialog(this)); break;
        case 1: settingsHub->setPage(new InputConfigDialog(this)); break;
        case 2: settingsHub->setPage(new VideoSettingsDialog(this)); break;
        case 3: settingsHub->setPage(new CameraSettingsDialog(this)); break;
        case 4: settingsHub->setPage(new AudioSettingsDialog(this)); break;
        case 5: settingsHub->setPage(new MPSettingsDialog(this)); break;
        case 6: settingsHub->setPage(new WifiSettingsDialog(this)); break;
        case 7: settingsHub->setPage(new FirmwareSettingsDialog(this)); break;
        case 8: settingsHub->setPage(new InterfaceSettingsDialog(this)); break;
        case 9: settingsHub->setPage(new PathSettingsDialog(this)); break;
        case 10: settingsHub->setPage(new DebugSettingsDialog(this)); break;
    }
}

void MainWindow::onOpenEmuSettings()
{
    emuThread->emuPause();

    EmuSettingsDialog* dlg = EmuSettingsDialog::openDlg(this);
    connect(dlg, &EmuSettingsDialog::finished, this, &MainWindow::onEmuSettingsDialogFinished);
}

void MainWindow::onEmuSettingsDialogFinished(int res)
{
    if (globalCfg.GetInt("Emu.ConsoleType") == 1)
    {
        actInsertGBACart->setEnabled(false);
        for (auto act : actInsertGBAAddon)
            act->setEnabled(false);
        actEjectGBACart->setEnabled(false);
    }
    else
    {
        actInsertGBACart->setEnabled(true);
        for (auto act : actInsertGBAAddon)
            act->setEnabled(true);
        actEjectGBACart->setEnabled(emuInstance->gbaCartInserted());
    }

    if (EmuSettingsDialog::needsReset)
        onReset();

    actCurrentGBACart->setText(tr("GBA slot: ") + emuInstance->gbaCartLabel());

    if (!emuThread->emuIsActive())
        actTitleManager->setEnabled(!globalCfg.GetString("DSi.NANDPath").empty());

    emuThread->emuUnpause();
}

void MainWindow::onOpenInputConfig()
{
    emuThread->emuPause();

    InputConfigDialog* dlg = InputConfigDialog::openDlg(this);
    connect(dlg, &InputConfigDialog::finished, this, &MainWindow::onInputConfigFinished);
    connect(dlg, &InputConfigDialog::mappingsChanged, this, &MainWindow::refreshKeyboardPreviews);
}

void MainWindow::onInputConfigFinished(int res)
{
    emuThread->emuUnpause();
    refreshKeyboardPreviews();
}

void MainWindow::onOpenVideoSettings()
{
    VideoSettingsDialog* dlg = VideoSettingsDialog::openDlg(this);
    connect(dlg, &VideoSettingsDialog::updateVideoSettings, this, &MainWindow::onUpdateVideoSettings);
}

void MainWindow::onOpenCameraSettings()
{
    emuThread->emuPause();

    camStarted[0] = camManager[0]->isStarted();
    camStarted[1] = camManager[1]->isStarted();
    if (camStarted[0]) camManager[0]->stop();
    if (camStarted[1]) camManager[1]->stop();

    CameraSettingsDialog* dlg = CameraSettingsDialog::openDlg(this);
    connect(dlg, &CameraSettingsDialog::finished, this, &MainWindow::onCameraSettingsFinished);
}

void MainWindow::onCameraSettingsFinished(int res)
{
    if (camStarted[0]) camManager[0]->start();
    if (camStarted[1]) camManager[1]->start();

    emuThread->emuUnpause();
}

void MainWindow::onOpenAudioSettings()
{
    AudioSettingsDialog* dlg = AudioSettingsDialog::openDlg(this);
    connect(emuThread, &EmuThread::syncVolumeLevel, dlg, &AudioSettingsDialog::onSyncVolumeLevel);
    connect(emuThread, &EmuThread::windowEmuStart, dlg, &AudioSettingsDialog::onConsoleReset);
    connect(dlg, &AudioSettingsDialog::updateAudioVolume, this, &MainWindow::onUpdateAudioVolume);
    connect(dlg, &AudioSettingsDialog::updateAudioSettings, this, &MainWindow::onUpdateAudioSettings);
    connect(dlg, &AudioSettingsDialog::finished, this, &MainWindow::onAudioSettingsFinished);
}

void MainWindow::onOpenFirmwareSettings()
{
    emuThread->emuPause();

    FirmwareSettingsDialog* dlg = FirmwareSettingsDialog::openDlg(this);
    connect(dlg, &FirmwareSettingsDialog::finished, this, &MainWindow::onFirmwareSettingsFinished);
}

void MainWindow::onFirmwareSettingsFinished(int res)
{
    if (FirmwareSettingsDialog::needsReset)
        onReset();

    emuThread->emuUnpause();
}

void MainWindow::onOpenPathSettings()
{
    emuThread->emuPause();

    PathSettingsDialog* dlg = PathSettingsDialog::openDlg(this);
    connect(dlg, &PathSettingsDialog::finished, this, &MainWindow::onPathSettingsFinished);
}

void MainWindow::onPathSettingsFinished(int res)
{
    if (PathSettingsDialog::needsReset)
        onReset();

    emuThread->emuUnpause();
}

void MainWindow::onUpdateAudioVolume(int vol, int dsisync)
{
    emuInstance->audioVolume = vol;
    emuInstance->audioDSiVolumeSync = dsisync;
}

void MainWindow::onUpdateAudioSettings()
{
    if (!emuThread->emuIsActive()) return;
    assert(emuInstance->nds != nullptr);

    int interp = globalCfg.GetInt("Audio.Interpolation");
    emuInstance->nds->SPU.SetInterpolation(static_cast<AudioInterpolation>(interp));

    int bitdepth = globalCfg.GetInt("Audio.BitDepth");
    if (bitdepth == 0)
        emuInstance->nds->SPU.SetDegrade10Bit(emuInstance->nds->ConsoleType == 0);
    else
        emuInstance->nds->SPU.SetDegrade10Bit(bitdepth == 1);
}

void MainWindow::onAudioSettingsFinished(int res)
{
    emuInstance->audioUpdateSettings();
}

void MainWindow::onOpenMPSettings()
{
    emuThread->emuPause();

    MPSettingsDialog* dlg = MPSettingsDialog::openDlg(this);
    connect(dlg, &MPSettingsDialog::finished, this, &MainWindow::onMPSettingsFinished);
}

void MainWindow::onMPSettingsFinished(int res)
{
    emuInstance->mpAudioMode = globalCfg.GetInt("MP.AudioMode");
    emuInstance->updateAudioMuteByWindowFocus();
    MPInterface::Get().SetRecvTimeout(globalCfg.GetInt("MP.RecvTimeout"));

    emuThread->emuUnpause();
}

void MainWindow::onOpenWifiSettings()
{
    emuThread->emuPause();

    WifiSettingsDialog* dlg = WifiSettingsDialog::openDlg(this);
    connect(dlg, &WifiSettingsDialog::finished, this, &MainWindow::onWifiSettingsFinished);
}

void MainWindow::onWifiSettingsFinished(int res)
{
    if (WifiSettingsDialog::needsReset)
        onReset();

    emuThread->emuUnpause();
}

void MainWindow::onOpenInterfaceSettings()
{
    emuThread->emuPause();
    InterfaceSettingsDialog* dlg = InterfaceSettingsDialog::openDlg(this);
    connect(dlg, &InterfaceSettingsDialog::finished, this, &MainWindow::onInterfaceSettingsFinished);
    connect(dlg, &InterfaceSettingsDialog::updateInterfaceSettings, this, &MainWindow::onUpdateInterfaceSettings);
}

void MainWindow::onUpdateInterfaceSettings()
{
    pauseOnLostFocus = globalCfg.GetBool("PauseLostFocus");
    emuInstance->targetFPS = globalCfg.GetDouble("TargetFPS");
    emuInstance->fastForwardFPS = globalCfg.GetDouble("FastForwardFPS");
    emuInstance->slowmoFPS = globalCfg.GetDouble("SlowmoFPS");
    panel->setMouseHide(globalCfg.GetBool("Mouse.Hide"),
                        globalCfg.GetInt("Mouse.HideSeconds")*1000);
}

void MainWindow::onInterfaceSettingsFinished(int res)
{
    emuThread->emuUnpause();
}

void MainWindow::onChangeScreenSize()
{
    int factor = ((QAction*)sender())->data().toInt();
    // If the window is maximized, resize() below is a no-op (Qt ignores
    // resizes on a maximized window), so the requested 1x/2x/3x/4x size
    // never actually applies -- the panel just keeps stretching to fill
    // the maximized window instead of the window returning to the
    // requested size. Leave maximized mode first so the resize sticks.
    if (isMaximized())
        showNormal();
    QSize diff = size() - panel->size();
    resize(panel->screenGetMinSize(factor) + diff);
}

void MainWindow::onChangeScreenRotation(QAction* act)
{
    int rot = act->data().toInt();
    windowCfg.SetInt("ScreenRotation", rot);

    emit screenLayoutChange();
}

void MainWindow::onChangeScreenGap(QAction* act)
{
    int gap = act->data().toInt();
    windowCfg.SetInt("ScreenGap", gap);

    emit screenLayoutChange();
}

void MainWindow::onChangeScreenLayout(QAction* act)
{
    int layout = act->data().toInt();
    windowCfg.SetInt("ScreenLayout", layout);

    emit screenLayoutChange();
}

void MainWindow::onChangeScreenSwap(bool checked)
{
    windowCfg.SetBool("ScreenSwap", checked);

    // Swap between top and bottom screen when displaying one screen.
    int sizing = windowCfg.GetInt("ScreenSizing");
    if (sizing == screenSizing_TopOnly)
    {
        // Bottom Screen.
        sizing = screenSizing_BotOnly;
        actScreenSizing[screenSizing_TopOnly]->setChecked(false);
        actScreenSizing[sizing]->setChecked(true);
    }
    else if (sizing == screenSizing_BotOnly)
    {
        // Top Screen.
        sizing = screenSizing_TopOnly;
        actScreenSizing[screenSizing_BotOnly]->setChecked(false);
        actScreenSizing[sizing]->setChecked(true);
    }
    windowCfg.SetInt("ScreenSizing", sizing);

    emit screenLayoutChange();
}

void MainWindow::onChangeScreenSizing(QAction* act)
{
    int sizing = act->data().toInt();
    windowCfg.SetInt("ScreenSizing", sizing);

    emit screenLayoutChange();
}

void MainWindow::onChangeScreenAspect(QAction* act)
{
    int aspect = act->data().toInt();
    QActionGroup* group = act->actionGroup();

    if (group == grpScreenAspectTop)
    {
        windowCfg.SetInt("ScreenAspectTop", aspect);
    }
    else
    {
        windowCfg.SetInt("ScreenAspectBot", aspect);
    }

    emit screenLayoutChange();
}

void MainWindow::onChangeIntegerScaling(bool checked)
{
    windowCfg.SetBool("IntegerScaling", checked);

    emit screenLayoutChange();
}

void MainWindow::onOpenNewWindow()
{
    emuInstance->createWindow();
}

void MainWindow::onChangeScreenFiltering(bool checked)
{
    windowCfg.SetBool("ScreenFilter", checked);

    //emit screenLayoutChange();
    panel->setFilter(checked);
}

void MainWindow::onChangeShowOSD(bool checked)
{
    showOSD = checked;
    panel->osdSetEnabled(showOSD);
    windowCfg.SetBool("ShowOSD", showOSD);
}

void MainWindow::onChangeShowKeyboardPreview(bool checked)
{
    showKeyboardPreview = checked;
    windowCfg.SetBool("ShowKeyboardPreview", showKeyboardPreview);
    if (liveKeyboardPreview)
    {
        if (checked) liveKeyboardPreview->refreshFromInstance(emuInstance);
        positionLiveKeyboardPreview();
    }
    updateLiveKeyboardPreviewVisibility();
}

void MainWindow::onChangeLimitFramerate(bool checked)
{
    emuInstance->doLimitFPS = checked;
    globalCfg.SetBool("LimitFPS", emuInstance->doLimitFPS);
}

void MainWindow::onChangeAudioSync(bool checked)
{
    emuInstance->doAudioSync = checked;
    globalCfg.SetBool("AudioSync", emuInstance->doAudioSync);
}


void MainWindow::onTitleUpdate(QString title)
{
    if (!emuInstance) return;

    int numinst = numEmuInstances();
    int numwin = emuInstance->getNumWindows();
    if ((numinst > 1) && (numwin > 1))
    {
        // add player/window prefix
        QString prefix = QString("[p%1:w%2] ").arg(emuInstance->instanceID+1).arg(windowID+1);
        title = prefix + title;
    }
    else if (numinst > 1)
    {
        // add player prefix
        QString prefix = QString("[p%1] ").arg(emuInstance->instanceID+1);
        title = prefix + title;
    }
    else if (numwin > 1)
    {
        // add window prefix
        QString prefix = QString("[w%1] ").arg(windowID+1);
        title = prefix + title;
    }

    setWindowTitle(title);
}

void MainWindow::toggleFullscreen()
{
    if (!isFullScreen())
    {
        showFullScreen();
        if (titleBarToolBar) titleBarToolBar->setVisible(false);
        if (topMenuToolBar) topMenuToolBar->setVisible(false);
    }
    else
    {
        showNormal();
        if (titleBarToolBar) titleBarToolBar->setVisible(true);
        if (topMenuToolBar) topMenuToolBar->setVisible(true);
    }

    if (resizeGrips) resizeGrips->updateGeometry();
}

// NOTE: this app previously clipped the frameless window to a rounded
// QRegion via setMask() here to hide the square backing rectangle's real
// corners. That's an X11-era mechanism Wayland compositors aren't required
// to honor, and on this system it applied inconsistently, which is what
// caused some corners to stay square. It's been removed in favor of
// MainWindow's WA_StyledBackground (see constructor) reliably painting
// only the QSS rounded shape every frame - no window-shape clipping
// needed for the visual result.
void MainWindow::paintEvent(QPaintEvent* event)
{
    // Every rounded-corner technique tried here - QRegion masking,
    // WA_TranslucentBackground, a manually-drawn rounded outline - was
    // unreliable on this system (no working compositor to blend/clip
    // against). The one thing that DOES render correctly here is a native
    // QMenu dropdown's own rounding, which goes through a completely
    // different Qt code path we don't control. For this window: stop
    // fighting it and just paint a flat, fully opaque rectangle. No mask,
    // no translucency, no drawn corner radius.
    QPainter p(this);
    p.fillRect(rect(), QColor(16, 18, 23));
    QMainWindow::paintEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (resizeGrips) resizeGrips->updateGeometry();
    positionTopMenuRestoreBtn();
    if (pauseMenuOverlay) pauseMenuOverlay->setGeometry(QRect(mapToGlobal(QPoint(0, 0)), size()));
    positionLiveKeyboardPreview();
}

void MainWindow::moveEvent(QMoveEvent* event)
{
    QMainWindow::moveEvent(event);
    if (pauseMenuOverlay) pauseMenuOverlay->setGeometry(QRect(mapToGlobal(QPoint(0, 0)), size()));
    positionLiveKeyboardPreview();
}

void MainWindow::positionLiveKeyboardPreview()
{
    if (!liveKeyboardPreview) return;

    // Anchor to the actual game screen widget, not MainWindow's own outer
    // rect - MainWindow's height includes the custom title bar/menu bar
    // chrome above the game area, which used to throw this off (it read
    // as sitting too low, relative to the visible screen, once this
    // switched to a real top-level window computing against the wrong
    // reference size).
    QWidget* anchor = (panel && panel->isVisible()) ? (QWidget*)panel : (QWidget*)this;
    QPoint corner = anchor->mapToGlobal(QPoint(anchor->width() - liveKeyboardPreview->width() - 16,
                                                anchor->height() - liveKeyboardPreview->height() - 16));
    liveKeyboardPreview->move(corner);
    liveKeyboardPreview->raise();
}

#ifdef Q_OS_WIN
void MainWindow::applyLiveKeyboardPreviewToolStyle()
{
    if (!liveKeyboardPreview) return;
    liveKeyboardPreview->winId(); // force native HWND creation now
    HWND kbHwnd = reinterpret_cast<HWND>(liveKeyboardPreview->winId());
    LONG_PTR exStyle = GetWindowLongPtr(kbHwnd, GWL_EXSTYLE);
    exStyle |= WS_EX_TOOLWINDOW;
    exStyle &= ~WS_EX_APPWINDOW;
    SetWindowLongPtr(kbHwnd, GWL_EXSTYLE, exStyle);
    // Belt-and-suspenders: force the real Win32 owner to MainWindow's HWND
    // by hand. An unowned WS_EX_TOOLWINDOW window can still earn its own
    // taskbar button on Windows 11, and Qt's parent->owner wiring doesn't
    // always survive things like a fullscreen HWND swap.
    winId(); // make sure MainWindow itself has a native HWND to point to
    HWND mainHwnd = reinterpret_cast<HWND>(winId());
    SetWindowLongPtr(kbHwnd, GWLP_HWNDPARENT, (LONG_PTR)mainHwnd);
    // SetWindowLongPtr alone does NOT take effect: per MSDN, a GWL_EXSTYLE
    // (or GWLP_HWNDPARENT) change needs an explicit SetWindowPos with
    // SWP_FRAMECHANGED afterwards, or Explorer keeps using the taskbar
    // button/grouping it decided on when the HWND was first created -
    // which is exactly the "still shows as its own entry" symptom.
    SetWindowPos(kbHwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}
#endif

void MainWindow::updateLiveKeyboardPreviewVisibility()
{
    if (!liveKeyboardPreview) return;
    // Only ever shown during actual gameplay: hidden in the library, and
    // hidden while the pause menu is up (which draws its own separate
    // preview - having both on screen at once is what used to overlap).
    bool inGame = !showingLibrary && emuInstance && emuInstance->emuIsActive() && !pauseMenuOverlay;
    bool visible = showKeyboardPreview && inGame;
    if (visible) positionLiveKeyboardPreview(); // re-anchor to the panel now that it's actually showing
    liveKeyboardPreview->setVisible(visible);
#ifdef Q_OS_WIN
    if (visible) applyLiveKeyboardPreviewToolStyle();
#endif
}

void MainWindow::refreshKeyboardPreviews()
{
    if (liveKeyboardPreview) liveKeyboardPreview->refreshFromInstance(emuInstance);
    if (pausedKeyboardPreview) pausedKeyboardPreview->refreshFromInstance(emuInstance);
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_NCCALCSIZE && msg->wParam == TRUE)
    {
        if (isMaximized())
        {
            // Default handling insets the client rect by Windows' invisible
            // resize-border margins, which (with no native frame to hide them
            // in) show up as real empty space along the top/sides once
            // maximized. Recompute the client rect from the monitor's work
            // area instead so it fills it exactly, with no leftover gap.
            HMONITOR monitor = MonitorFromWindow(msg->hwnd, MONITOR_DEFAULTTONEAREST);
            if (monitor)
            {
                MONITORINFO info;
                info.cbSize = sizeof(MONITORINFO);
                if (GetMonitorInfoW(monitor, &info))
                {
                    NCCALCSIZE_PARAMS* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
                    params->rgrc[0] = info.rcWork;
                    *result = 0;
                    return true;
                }
            }
        }
        else
        {
            // Restored (non-maximized) state: even with WS_CAPTION cleared,
            // WS_THICKFRAME (kept for OS-native resize/snap) makes the
            // default handler still inset the client rect by the invisible
            // resize-border padding (SM_CXSIZEFRAME/SM_CYSIZEFRAME). With no
            // native caption to hide it in, that padding renders as a thin
            // white sliver above our own CustomTitleBar. We already draw our
            // own resize grips (WindowResizeGrips), so the native border
            // isn't needed for resizing -- just leave the proposed rect
            // (msg->lParam's rgrc[0], already the full window rect) as-is
            // and skip the default inset entirely.
            *result = 0;
            return true;
        }
    }

    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void MainWindow::positionTopMenuRestoreBtn()
{
    if (!topMenuRestoreBtn) return;

    // Right-aligned, just under the title bar - NOT inside it, or it
    // overlaps the min/max/close buttons that live there (CustomTitleBar,
    // 34px tall). Sits in the strip the collapsed menu row used to occupy.
    int titleBarH = titleBar ? titleBar->height() : 34;
    int margin = 8;
    topMenuRestoreBtn->move(width() - topMenuRestoreBtn->width() - margin, titleBarH + margin);
}

namespace
{
    // Full-window dimmer behind the pause menu buttons. Much lighter than
    // a flat black overlay: the frozen game frame should still read
    // clearly through it, not get buried under an opaque layer.
    //
    // This is a genuine top-level window (Qt::Tool + WA_TranslucentBackground),
    // not a plain child widget layered over ScreenPanelGL. The game view
    // renders through a native/GL surface that bypasses normal Qt widget
    // compositing, so a non-native child widget drawn "on top" of it in
    // the widget tree doesn't reliably alpha-blend against it on every
    // platform - it can end up looking fully opaque instead of see-through.
    // Promoting the overlay to its own translucent top-level window lets
    // the window manager/compositor do that blending instead, which does
    // work reliably across native surfaces.
    class PauseMenuDimmer : public QWidget
    {
    public:
        PauseMenuDimmer(QWidget* parent, std::function<void()> onEscape)
            : QWidget(parent), onEscape(onEscape)
        {
            setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
            setAttribute(Qt::WA_TranslucentBackground);
            setAttribute(Qt::WA_DeleteOnClose, false); // lifetime is managed by MainWindow, not by close()
        }

    protected:
        void paintEvent(QPaintEvent*) override
        {
            QPainter p(this);
            p.fillRect(rect(), QColor(6, 8, 14, 92));
        }

        void keyPressEvent(QKeyEvent* event) override
        {
            // This window (not MainWindow) now owns keyboard focus while
            // open, so Escape has to be caught here instead.
            if (!event->isAutoRepeat() && event->key() == Qt::Key_Escape)
            {
                if (onEscape) onEscape();
                return;
            }
            QWidget::keyPressEvent(event);
        }

    private:
        std::function<void()> onEscape;
    };
}

void MainWindow::togglePauseMenu()
{
    if (pauseMenuOverlay)
    {
        closePauseMenu();
        return;
    }

    emuThread->emuPause();

    pauseMenuOverlay = new PauseMenuDimmer(this, [this]() { closePauseMenu(); });
    pauseMenuOverlay->setGeometry(QRect(mapToGlobal(QPoint(0, 0)), size()));

    auto* outer = new QVBoxLayout(pauseMenuOverlay);
    outer->addStretch();

    auto* box = new QWidget(pauseMenuOverlay);
    box->setFixedWidth(240);
    // Plain QWidgets don't paint their stylesheet background at all by
    // default (that's opt-in) - without this, "background: rgba(...);
    // border-radius: 14px;" below either doesn't render or renders
    // square, which is the same corner bug fixed in SettingsHubDialog.
    box->setAttribute(Qt::WA_StyledBackground, true);
    // Frameless glass panel: no border at all (per request), just a soft
    // translucent fill so the paused frame behind it stays visible through
    // the whole box, not just the dimmer around it.
    box->setStyleSheet(
        "QWidget { background: rgba(24,26,34,150); border-radius: 14px; border: none; }"
        "QPushButton { color: white; background: rgba(255,255,255,25); "
        "  border: none; border-radius: 8px; padding: 11px; font-size: 13px; }"
        "QPushButton:hover { background: rgba(255,255,255,60); }"
        "QPushButton:disabled { color: rgba(255,255,255,90); background: rgba(255,255,255,10); }");
    auto* boxLayout = new QVBoxLayout(box);
    boxLayout->setSpacing(8);
    boxLayout->setContentsMargins(18, 18, 18, 18);

    auto* title = new QLabel(tr("Paused"), box);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("QLabel { color: white; font-size: 15px; font-weight: bold; "
                          "background: transparent; padding-bottom: 6px; }");
    boxLayout->addWidget(title);

    auto* btnResume = new QPushButton(tr("Resume"), box);
    connect(btnResume, &QPushButton::clicked, this, &MainWindow::closePauseMenu);
    boxLayout->addWidget(btnResume);

    // Quick-slot 1, same as the F1/Shift+F1 shortcuts - the pause menu
    // is meant to be a fast, no-submenu path, not a full save-state
    // manager (that's still in the regular menu for picking slots).
    auto* btnSave = new QPushButton(tr("Save State (slot 1)"), box);
    connect(btnSave, &QPushButton::clicked, this, [this]()
    {
        actSaveState[1]->trigger();
        closePauseMenu();
    });
    boxLayout->addWidget(btnSave);

    auto* btnLoad = new QPushButton(tr("Load State (slot 1)"), box);
    btnLoad->setEnabled(actLoadState[1]->isEnabled());
    connect(btnLoad, &QPushButton::clicked, this, [this]()
    {
        actLoadState[1]->trigger();
        closePauseMenu();
    });
    boxLayout->addWidget(btnLoad);

    auto* btnSettings = new QPushButton(tr("Settings"), box);
    connect(btnSettings, &QPushButton::clicked, this, [this]()
    {
        closePauseMenu();
        onOpenSettingsHub();
    });
    boxLayout->addWidget(btnSettings);

    auto* btnQuit = new QPushButton(tr("Stop"), box);
    connect(btnQuit, &QPushButton::clicked, this, [this]()
    {
        // Stops/unloads the current ROM (same as menu File > Stop) rather
        // than exiting the whole emulator - closePauseMenu() first since
        // onStop() will tear down emuThread state the overlay's Save/Load
        // buttons still hold references into.
        closePauseMenu();
        onStop();
    });
    boxLayout->addWidget(btnQuit);

    auto* boxRow = new QHBoxLayout();
    boxRow->addStretch();
    boxRow->addWidget(box);
    boxRow->addStretch();
    outer->addLayout(boxRow);
    outer->addStretch();

    // Keyboard mapping preview, bottom-right corner of the pause overlay:
    // every key currently bound to a DS button/hotkey lights up blue so the
    // player can see their whole layout at a glance (hover a blue key to
    // see what it's bound to).
    auto* kbPreview = new KeyboardPreviewWidget(pauseMenuOverlay);
    kbPreview->setFixedSize(400, 140);
    kbPreview->refreshFromInstance(emuInstance);
    pausedKeyboardPreview = kbPreview;
    // The overlay owns kbPreview and will delete it along with itself in
    // closePauseMenu; drop our pointer to it at that same moment so we
    // never touch a dangling widget.
    connect(kbPreview, &QObject::destroyed, this, [this]() { pausedKeyboardPreview = nullptr; });

    // Deliberately NOT laid out via outer/kbRow: that anchored to the
    // whole overlay's own rect (title bar/menu chrome and all), while the
    // docked in-game preview anchors to just the game screen (`panel`).
    // Different reference rect + different margins (18/14 vs 16/16) meant
    // this jumped to a different spot than the live one on pause/unpause.
    // Position it with the exact same corner formula, against the same
    // anchor, so it lands pixel-for-pixel where the live one just was.
    QWidget* kbAnchor = (panel && panel->isVisible()) ? (QWidget*)panel : (QWidget*)this;
    QPoint kbCorner = kbAnchor->mapToGlobal(QPoint(kbAnchor->width() - kbPreview->width() - 16,
                                                    kbAnchor->height() - kbPreview->height() - 16));
    kbPreview->move(pauseMenuOverlay->mapFromGlobal(kbCorner));
    kbPreview->raise();

    // The docked in-game preview and this one would otherwise overlap in
    // the same corner - only one is ever shown at a time.
    updateLiveKeyboardPreviewVisibility();

    // Fade the whole overlay (dimmer + panel together) in rather than
    // popping it in instantly - reads much less jarring mid-gameplay.
    // windowOpacity rather than QGraphicsOpacityEffect, since this is now
    // a genuine top-level window (see PauseMenuDimmer comment above) and
    // graphics effects on top-level widgets aren't reliably supported.
    pauseMenuOverlay->setWindowOpacity(0.0);

    auto* fadeIn = new QPropertyAnimation(pauseMenuOverlay, "windowOpacity", pauseMenuOverlay);
    fadeIn->setDuration(160);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);
    fadeIn->setEasingCurve(QEasingCurve::OutCubic);

    pauseMenuOverlay->show();
    pauseMenuOverlay->raise();
    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
    btnResume->setFocus();
}

void MainWindow::closePauseMenu()
{
    if (!pauseMenuOverlay) return;

    QWidget* overlay = pauseMenuOverlay;
    pauseMenuOverlay = nullptr; // reflects "closed" immediately, so a
                                // second Escape mid-fade-out re-opens
                                // cleanly instead of trying to fade an
                                // overlay that's already on its way out
    pausedKeyboardPreview = nullptr; // its widget is going away with the
                                      // overlay (see destroyed() connection above)

    auto* fadeOut = new QPropertyAnimation(overlay, "windowOpacity", overlay);
    fadeOut->setDuration(120);
    fadeOut->setStartValue(overlay->windowOpacity());
    fadeOut->setEndValue(0.0);
    fadeOut->setEasingCurve(QEasingCurve::InCubic);
    connect(fadeOut, &QPropertyAnimation::finished, overlay, &QWidget::deleteLater);
    fadeOut->start(QAbstractAnimation::DeleteWhenStopped);

    emuThread->emuUnpause();
    updateLiveKeyboardPreviewVisibility(); // the docked preview can come back now
}

void MainWindow::changeEvent(QEvent* event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
    {
        if (titleBar) titleBar->refreshMaximizeGlyph();
        if (resizeGrips) resizeGrips->updateGeometry();
    }
}

void MainWindow::onFullscreenToggled()
{
    toggleFullscreen();
}

void MainWindow::onScreenEmphasisToggled()
{
    int currentSizing = windowCfg.GetInt("ScreenSizing");
    if (currentSizing == screenSizing_EmphTop)
    {
        currentSizing = screenSizing_EmphBot;
    }
    else if (currentSizing == screenSizing_EmphBot)
    {
        currentSizing = screenSizing_EmphTop;
    }
    else
    {
        // For any other sizing mode, switch to EmphTop as a sensible default
        currentSizing = screenSizing_EmphTop;
    }
    windowCfg.SetInt("ScreenSizing", currentSizing);
    actScreenSizing[currentSizing]->setChecked(true);

    emit screenLayoutChange();
}

void MainWindow::onEmuStart()
{
    showingLibrary = false;
    if (centralStack) centralStack->setCurrentWidget(panel);
    if (liveKeyboardPreview) liveKeyboardPreview->raise();
    updateLiveKeyboardPreviewVisibility();
#if defined(Q_OS_WIN)
    // See createScreenPanel() for why this is needed on Windows only.
    if (library) library->setVisible(false);
    if (panel) panel->setVisible(true);
    QTimer::singleShot(0, this, [this]()
    {
        if (!panel || !centralStack) return;
        panel->raise();
        centralStack->repaint();
    });
#endif

    if (!hasMenu) return;

    for (int i = 1; i < 9; i++)
    {
        actSaveState[i]->setEnabled(true);
        actLoadState[i]->setEnabled(emuInstance->savestateExists(i));
    }
    actSaveState[0]->setEnabled(true);
    actLoadState[0]->setEnabled(true);
    actUndoStateLoad->setEnabled(false);

    actPause->setEnabled(true);
    actPause->setChecked(false);
    actReset->setEnabled(true);
    actStop->setEnabled(true);
    actFrameStep->setEnabled(true);

    //actDateTime->setEnabled(false);
    actPowerManagement->setEnabled(true);

    actTitleManager->setEnabled(false);
}

void MainWindow::onEmuStop()
{
    showingLibrary = true;
    if (centralStack) centralStack->setCurrentWidget(library);
    if (liveKeyboardPreview) liveKeyboardPreview->raise();
    updateLiveKeyboardPreviewVisibility();
#if defined(Q_OS_WIN)
    // See createScreenPanel() for why this is needed on Windows only.
    if (panel) panel->setVisible(false);
    if (library) library->setVisible(true);
    QTimer::singleShot(0, this, [this]()
    {
        if (!library || !centralStack) return;
        library->raise();
        centralStack->repaint();
    });
#endif

    if (!hasMenu) return;

    for (int i = 0; i < 9; i++)
    {
        actSaveState[i]->setEnabled(false);
        actLoadState[i]->setEnabled(false);
    }
    actUndoStateLoad->setEnabled(false);

    actPause->setEnabled(false);
    actReset->setEnabled(false);
    actStop->setEnabled(false);
    actFrameStep->setEnabled(false);

    //actDateTime->setEnabled(true);
    actPowerManagement->setEnabled(false);

    actTitleManager->setEnabled(!globalCfg.GetString("DSi.NANDPath").empty());
}

void MainWindow::onEmuPause(bool pause)
{
    if (!hasMenu) return;

    actPause->setChecked(pause);
}

void MainWindow::onEmuReset()
{
    if (!hasMenu) return;

    actUndoStateLoad->setEnabled(false);
}

void MainWindow::onUpdateVideoSettings(bool glchange)
{
    if (!emuInstance) return;

    // if we have a parent window: pass the message over to the parent
    // the topmost parent takes care of updating all the windows
    MainWindow* parentwin = (MainWindow*)parentWidget();
    if (parentwin)
        return parentwin->onUpdateVideoSettings(glchange);

    auto childwins = findChildren<MainWindow *>(nullptr);

    bool hadOGL = hasOGL;
    if (glchange)
    {
        emuThread->emuPause();
        if (hadOGL)
        {
            emuThread->deinitContext(windowID);
            for (auto child: childwins)
            {
                auto thread = child->getEmuInstance()->getEmuThread();
                thread->deinitContext(child->windowID);
            }
        }

        createScreenPanel();
        for (auto child: childwins)
        {
            child->createScreenPanel();
        }
    }

    emuThread->updateVideoSettings();
    for (auto child: childwins)
    {
        // child windows may belong to a different instance
        // in that case we need to signal their thread appropriately
        auto thread = child->getEmuInstance()->getEmuThread();
        if (child->getWindowID() == 0)
            thread->updateVideoSettings();
    }

    if (glchange)
    {
        if (hasOGL) 
        {
            emuThread->initContext(windowID);
            for (auto child: childwins)
            {
                auto thread = child->getEmuInstance()->getEmuThread();
                thread->initContext(child->windowID);
            }
        }
    }

    if (glchange)
    {
        emuThread->emuUnpause();
    }
}
