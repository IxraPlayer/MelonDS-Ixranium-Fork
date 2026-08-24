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

#ifndef WINDOW_H
#define WINDOW_H

#include "glad/glad.h"
#include "ScreenLayout.h"
#include "duckstation/gl/context.h"

#include <QWidget>
#include <QWindow>
#include <QMainWindow>
#include <QStackedWidget>
#include <QImage>
#include <QActionGroup>
#include <QTimer>
#include <QMutex>
#include <QScreen>
#include <QCloseEvent>
#include <QToolButton>

#include "Screen.h"
#include "LibraryScreen.h"
#include "Config.h"
#include "MPInterface.h"


class EmuInstance;
class EmuThread;
class CustomTitleBar;
class WindowResizeGrips;
class TopMenuBar;
class QToolBar;

const int kMaxRecentROMs = 10;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(int id, EmuInstance* inst, QWidget* parent = nullptr);
    ~MainWindow();

    EmuInstance* getEmuInstance() { return emuInstance; }
    Config::Table& getWindowConfig() { return windowCfg; }
    int getWindowID() { return windowID; }

    bool winHasMenu() { return hasMenu; }

    void saveEnabled(bool enabled);

    void toggleFullscreen();

    bool hasOpenGL() { return hasOGL; }
    GL::Context* getOGLContext();
    void initOpenGL();
    void deinitOpenGL();
    void setGLSwapInterval(int intv);
    void makeCurrentGL();
    void releaseGL();

    void drawScreen();

    bool preloadROMs(QStringList file, QStringList gbafile, bool boot);
    QStringList splitArchivePath(const QString& filename, bool useMemberSyntax);

    void onAppStateChanged(Qt::ApplicationState state);

    void onFocusIn();
    void onFocusOut();
    bool isFocused() { return focused; }

    void osdAddMessage(unsigned int color, const char* msg);
    void toggleDebugOverlay();

    // called when the MP interface is changed
    void updateMPInterface(melonDS::MPInterfaceType type);

    void loadRecentFilesMenu(bool loadcfg);
    //void updateVideoSettings(bool glchange);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
#ifdef Q_OS_WIN
    // On Windows, a Qt::FramelessWindowHint window that gets maximized via
    // the OS (snap, taskbar, our own restore/maximize button) still has
    // Windows' invisible resize-border margins applied to it. Since there's
    // no native frame to absorb them, they show up as extra empty space
    // along the top (and sides) of the window instead. Intercepting
    // WM_NCCALCSIZE lets us cancel that margin out while maximized.
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

signals:
    void screenLayoutChange();

private slots:
    void onOpenFile();
    void onClickRecentFile();
    void onClearRecentFiles();
    void onBootFirmware();
    void onInsertCart();
    void onEjectCart();
    void onInsertGBACart();
    void onInsertGBAAddon();
    void onEjectGBACart();
    void onSaveState();
    void onLoadState();
    void onUndoStateLoad();
    void onImportSavefile();
    void onQuit();

    void onPause(bool checked);
    void onReset();
    void onStop();
    void onFrameStep();
    void onOpenPowerManagement();
    void onOpenDateTime();
    void onDateTimeDialogFinished(int res);
    void onEnableCheats(bool checked);
    void onSetupCheats();
    void onCheatsDialogFinished(int res);
    void onROMInfo();
    void onRAMInfo();
    void onOpenTitleManager();
    void onMPNewInstance();
    void onLANStartHost();
    void onLANStartClient();
    void onNPStartHost();
    void onNPStartClient();
    void onNPTest();

    void onOpenEmuSettings();
    void onEmuSettingsDialogFinished(int res);
    void onOpenSettingsHub();
    void onSettingsHubCategory(int index);
    void onLibraryGameActivated(QString path);
    void onLibraryAddGameRequested();
    void saveLibraryToConfig();
    void onOpenInputConfig();
    void onInputConfigFinished(int res);
    void onOpenVideoSettings();
    void onOpenCameraSettings();
    void onCameraSettingsFinished(int res);
    void onOpenAudioSettings();
    void onUpdateAudioVolume(int vol, int dsisync);
    void onUpdateAudioSettings();
    void onAudioSettingsFinished(int res);
    void onOpenMPSettings();
    void onMPSettingsFinished(int res);
    void onOpenWifiSettings();
    void onWifiSettingsFinished(int res);
    void onOpenFirmwareSettings();
    void onFirmwareSettingsFinished(int res);
    void onOpenPathSettings();
    void onPathSettingsFinished(int res);
    void onOpenInterfaceSettings();
    void onInterfaceSettingsFinished(int res);
    void onUpdateInterfaceSettings();
    void onChangeScreenSize();
    void onChangeScreenRotation(QAction* act);
    void onChangeScreenGap(QAction* act);
    void onChangeScreenLayout(QAction* act);
    void onChangeScreenSwap(bool checked);
    void onChangeScreenSizing(QAction* act);
    void onChangeScreenAspect(QAction* act);
    void onChangeIntegerScaling(bool checked);
    void onOpenNewWindow();
    void onChangeScreenFiltering(bool checked);
    void onChangeShowOSD(bool checked);
    void onChangeLimitFramerate(bool checked);
    void onChangeAudioSync(bool checked);

    void onTitleUpdate(QString title);

    void onEmuStart();
    void onEmuStop();
    void onEmuPause(bool pause);
    void onEmuReset();

    void onUpdateVideoSettings(bool glchange);

    void onFullscreenToggled();
    void onScreenEmphasisToggled();

private:
    virtual void closeEvent(QCloseEvent* event) override;

    QStringList currentROM;
    QStringList currentGBAROM;
    QList<QString> recentFileList;
    QMenu *recentMenu;
    void updateRecentFilesMenu();

    bool verifySetup();
    QString pickFileFromArchive(QString archiveFileName);
    QStringList pickROM(bool gba);
    QString installGameToLibrary(const QStringList& file);
    QString detectDesktopPath();
    void createDesktopShortcut(const QString& gameName, const QString& gamePath);
    void updateCartInserted(bool gba);

    void createScreenPanel();

    bool lanWarning(bool host);

    bool showOSD;

    bool hasOGL;

    bool pauseOnLostFocus;
    bool pausedManually;

    int windowID;
    bool enabledSaved;

    bool focused;

    EmuInstance* emuInstance;
    EmuThread* emuThread;

    Config::Table& globalCfg;
    Config::Table& localCfg;
    Config::Table windowCfg;

public:
    ScreenPanel* panel;
    CustomTitleBar* titleBar = nullptr;
    QToolBar* titleBarToolBar = nullptr;
    WindowResizeGrips* resizeGrips = nullptr;
    TopMenuBar* topMenuBar = nullptr;
    QToolBar* topMenuToolBar = nullptr;

    // Small floating "bring the menu back" arrow, shown in the top-right
    // corner only while topMenuToolBar is hidden (see TopMenuBar's
    // collapseClicked() / onTopMenuCollapseClicked() below).
    QToolButton* topMenuRestoreBtn = nullptr;
    void positionTopMenuRestoreBtn();

    // ESC pause menu: a simple full-window overlay (Resume/Save
    // state/Load state/Settings/Quit) toggled by the Escape key while a
    // ROM is running. Built on demand in togglePauseMenu() rather than a
    // separate widget file - it's just a handful of buttons on a dimmed
    // background.
    QWidget* pauseMenuOverlay = nullptr;
    void togglePauseMenu();
    void closePauseMenu();
    QStackedWidget* centralStack = nullptr;
    LibraryScreen* library = nullptr;
    bool showingLibrary = true;

    bool hasMenu;

    QAction* actOpenROM;
    QAction* actBootFirmware;
    QAction* actCurrentCart;
    QAction* actInsertCart;
    QAction* actEjectCart;
    QAction* actCurrentGBACart;
    QAction* actInsertGBACart;
    QList<QAction*> actInsertGBAAddon;
    QAction* actEjectGBACart;
    QAction* actImportSavefile;
    QAction* actSaveState[9];
    QAction* actLoadState[9];
    QAction* actUndoStateLoad;
    QAction* actOpenConfig;
    QAction* actQuit;

    QAction* actPause;
    QAction* actReset;
    QAction* actStop;
    QAction* actFrameStep;
    QAction* actPowerManagement;
    QAction* actDateTime;
    QAction* actEnableCheats;
    QAction* actSetupCheats;
    QAction* actROMInfo;
    QAction* actRAMInfo;
    QAction* actTitleManager;
    QAction* actMPNewInstance;
    QAction* actLANStartHost;
    QAction* actLANStartClient;
    QAction* actNPStartHost;
    QAction* actNPStartClient;
    QAction* actNPTest;

    QAction* actEmuSettings;
    QAction* actSettingsHub;
    class SettingsHubDialog* settingsHub = nullptr;
#ifdef __APPLE__
    QAction* actPreferences;
#endif
    QAction* actInputConfig;
    QAction* actVideoSettings;
    QAction* actCameraSettings;
    QAction* actAudioSettings;
    QAction* actMPSettings;
    QAction* actWifiSettings;
    QAction* actFirmwareSettings;
    QAction* actPathSettings;
    QAction* actInterfaceSettings;
    QAction* actScreenSize[4];
    QActionGroup* grpScreenRotation;
    QAction* actScreenRotation[screenRot_MAX];
    QActionGroup* grpScreenGap;
    QAction* actScreenGap[6];
    QActionGroup* grpScreenLayout;
    QAction* actScreenLayout[screenLayout_MAX];
    QAction* actScreenSwap;
    QActionGroup* grpScreenSizing;
    QAction* actScreenSizing[screenSizing_MAX];
    QAction* actIntegerScaling;
    QActionGroup* grpScreenAspectTop;
    QAction** actScreenAspectTop;
    QActionGroup* grpScreenAspectBot;
    QAction** actScreenAspectBot;
    QAction* actNewWindow;
    QAction* actScreenFiltering;
    QAction* actShowOSD;
    QAction* actLimitFramerate;
    QAction* actAudioSync;

    QAction* actAbout;
};

#endif // WINDOW_H
