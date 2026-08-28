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

#ifndef SCREEN_H
#define SCREEN_H

#include <optional>
#include <deque>
#include <map>
#include <atomic>

#include <QWidget>
#include <QImage>
#include <QMutex>
#include <QScreen>
#include <QCloseEvent>
#include <QTimer>

#include "glad/glad.h"
#include "ScreenLayout.h"
#include "duckstation/gl/context.h"


class MainWindow;
class EmuInstance;


const struct { int id; float ratio; const char* label; } aspectRatios[] =
{
    { 0, 1,                       "4:3 (native)" },
    { 4, (5.f  / 3) / (4.f / 3), "5:3 (3DS)"},
    { 1, (16.f / 9) / (4.f / 3),  "16:9" },
    { 2, (21.f / 9) / (4.f / 3),  "21:9" },
    { 3, 0,                       "window" }
};
constexpr int AspectRatiosNum = sizeof(aspectRatios) / sizeof(aspectRatios[0]);


class ScreenPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ScreenPanel(QWidget* parent);
    virtual ~ScreenPanel();

    void setFilter(bool filter);
    void setSharpUpscale(bool enable) { sharpUpscale = enable; update(); }
    void setPixelScale(int scale) { pixelScale = scale; update(); }

    void setMouseHide(bool enable, int delay);

    QTimer* setupMouseTimer();
    void updateMouseTimer();
    QTimer* mouseTimer;
    QSize screenGetMinSize(int factor);

    void osdSetEnabled(bool enabled);
    void osdAddMessage(unsigned int color, const char* msg);

    // Debug overlay (FPS/CPU/RAM), toggled via the hotkey assigned in
    // Settings > Debug settings. Lives on the base ScreenPanel and is
    // drawn through the shared OSD pipeline (see debugOverlayItems below)
    // so both the native (QPainter) and GL renderers show it identically.
    void setDebugOverlayVisible(bool visible);
    bool debugOverlayVisible() const;

    // Live in-game keyboard-mapping preview, rendered through this exact
    // same OSD bitmap pipeline as the debug overlay above (bottom-right
    // corner) instead of as a separate top-level widget. The previous
    // top-level-window approach kept getting its own taskbar button and
    // stealing activation/z-order from this panel on Windows, no matter how
    // many native-window workarounds were layered on - because it *was* a
    // second real window. Drawing it as part of this panel's own paint/GL
    // pass removes the second window entirely, so there is nothing left
    // that Explorer/DWM could ever treat as a separate app. The bitmap
    // itself is produced elsewhere (MainWindow grabs an offscreen,
    // never-shown KeyboardPreviewWidget into a QImage) and just handed to
    // us here to composite, the same way an OSDItem's bitmap is produced
    // by text rendering and then composited generically.
    void setKbPreviewImage(const QImage& img);
    void setKbPreviewVisible(bool visible);

    virtual void drawScreen() {}// = 0;

private slots:
    void onScreenLayoutChanged();
    void onAutoScreenSizingChanged(int sizing);

protected:
    // Written from EmuThread (via EmuInstance::toggleDebugOverlay() ->
    // MainWindow::toggleDebugOverlay() -> setDebugOverlayVisible(), a plain
    // direct cross-thread call, not a queued connection) and read from the
    // GUI/render thread in debugOverlayTick()/paintEvent(). A plain bool here
    // would be an unsynchronized data race; atomic<bool> makes the read/write
    // well-defined without needing a mutex for a single flag.
    std::atomic<bool> debugOverlayVisible_;


    MainWindow* mainWindow;
    EmuInstance* emuInstance;

    bool filter;

    int screenRotation;
    int screenGap;
    int screenLayout;
    bool screenSwap;
    int screenSizing;
    bool screenFocused;
    bool sharpUpscale = false;
    int pixelScale = 1;
    bool integerScaling;
    int screenAspectTop, screenAspectBot;

    int autoScreenSizing;

    ScreenLayout layout;
    float screenMatrix[kMaxScreenTransforms][6];
    int screenKind[kMaxScreenTransforms];
    int numScreens;

    bool touching = false;

    bool mouseHide;
    int mouseHideDelay;

    struct OSDItem
    {
        unsigned int id;
        qint64 timestamp;

        char text[256];
        unsigned int color;

        bool rendered;
        QImage bitmap;

        int rainbowstart;
        int rainbowend;
    };

    QMutex osdMutex;
    bool osdEnabled;
    unsigned int osdID;
    std::deque<OSDItem> osdItems;

    QPixmap splashLogo;
    OSDItem splashText[3];
    QPoint splashPos[4];

    // Debug overlay (FPS/CPU/RAM/etc). Rendered through the very same
    // OSD bitmap/texture pipeline as regular OSD messages and the splash
    // text (see osdRenderItem/osdDeleteItem below), instead of a separate
    // native QLabel widget. It used to be a QLabel sibling that got its
    // text/visibility poked once a second from a QTimer on the GUI
    // thread, while ScreenPanelGL::drawScreen() calls glContext->
    // SwapBuffers() directly to the same native window surface from
    // EmuThread (a *different* thread) many times per second. Those two
    // things race with no synchronization: whichever one the window
    // server/compositor happens to present last wins, so the label's
    // fresh pixels were getting stomped by the next emulator frame
    // almost immediately, which is why the overlay looked frozen/never
    // updated live. Piggybacking on osdItems avoids this: the bitmap is
    // (re)rendered inside osdUpdate(), which already runs on whichever
    // thread is actually producing that frame (EmuThread for GL, the GUI
    // thread for the QPainter/native path), so the text is always drawn
    // as part of the same frame that gets presented - no separate
    // surface, no cross-thread race.
    //
    // One OSDItem per enabled field (not one item with embedded '\n's):
    // the OSD bitmap-font renderer has no concept of a line break
    // character - it only wraps text by pixel width - so a '\n' inside
    // item.text just gets drawn as the "unknown glyph" box. Keeping each
    // field as its own item and stacking them (y += bitmap.height(), the
    // same way the regular OSD message list below already does) avoids
    // that entirely and keeps them cleanly stacked with no gap/box
    // between them.
    std::deque<OSDItem> debugOverlayItems;
    qint64 debugOverlayLastUpdate;

    // See setKbPreviewImage()/setKbPreviewVisible() above. kbPreviewDirty
    // tracks whether the GL subclass needs to re-upload the texture (the
    // native/QPainter subclass just draws kbPreviewImage directly every
    // frame and doesn't need it, hence the no-op base virtual).
    QImage kbPreviewImage;
    std::atomic<bool> kbPreviewVisible_ { false };
    bool kbPreviewDirty = true;
    virtual void kbPreviewTextureUpload() {}

    void loadConfig();

    virtual void setupScreenLayout();

    void resizeEvent(QResizeEvent* event) override;

    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

    void tabletEvent(QTabletEvent* event) override;
    void touchEvent(QTouchEvent* event);
    bool event(QEvent* event) override;

    void showCursor();

    int osdFindBreakPoint(const char* text, int i);
    void osdLayoutText(const char* text, int* width, int* height, int* breaks);
    unsigned int osdRainbowColor(int inc);

    virtual void osdRenderItem(OSDItem* item);
    virtual void osdDeleteItem(OSDItem* item);

    void osdUpdate();

    void calcSplashLayout();

    // Rebuilds debugOverlayItems from the current field mask/values.
    // Called from within drawScreen()/paintEvent() (see debugOverlayTick()
    // below), i.e. on whatever thread actually renders this panel -
    // never via a QTimer, since HK_ToggleDebugOverlay (and therefore
    // setDebugOverlayVisible()) is handled on EmuThread while a QTimer
    // created here belongs to the GUI thread. Starting/stopping a QTimer
    // from a thread other than the one it lives on is undefined by Qt's
    // own rules (start()/stop() aren't thread-safe across affinities),
    // so the old QTimer-based version could silently never fire - which
    // is why the overlay looked permanently frozen after the first paint.
    void updateDebugOverlayText();

    // Refreshes debugOverlayItems at most once a second. Call this from
    // the render path itself.
    void debugOverlayTick();
};


class ScreenPanelNative : public ScreenPanel
{
    Q_OBJECT

public:
    explicit ScreenPanelNative(QWidget* parent);
    virtual ~ScreenPanelNative();

    void drawScreen() override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setupScreenLayout() override;

    QMutex bufferLock;
    bool hasBuffers;
    void* topBuffer;
    void* bottomBuffer;

    QImage screen[2];
    QTransform screenTrans[kMaxScreenTransforms];
};


class ScreenPanelGL : public ScreenPanel
{
    Q_OBJECT

public:
    explicit ScreenPanelGL(QWidget* parent);
    virtual ~ScreenPanelGL();

    std::optional<WindowInfo> getWindowInfo();

    bool createContext();

    void setSwapInterval(int intv);

    void initOpenGL();
    void deinitOpenGL();
    void makeCurrentGL();
    void releaseGL();

    void drawScreen() override;

    GL::Context* getContext() { return glContext.get(); }

    void transferLayout();
protected:

    qreal devicePixelRatioFromScreen() const;
    int scaledWindowWidth() const;
    int scaledWindowHeight() const;

    QPaintEngine* paintEngine() const override;

private:
    void setupScreenLayout() override;

    std::unique_ptr<GL::Context> glContext;
    bool glInited;

    GLuint screenVertexBuffer, screenVertexArray;
    GLuint screenTexture;
    GLuint screenShaderProgram;
    GLint screenShaderTransformULoc, screenShaderScreenSizeULoc;
    GLint screenShaderSharpUpscaleULoc;
    GLint screenShaderPixelScaleULoc;

    QMutex screenSettingsLock;
    WindowInfo windowInfo;

    int lastScreenWidth = -1, lastScreenHeight = -1;

    GLuint osdShader;
    GLint osdScreenSizeULoc, osdPosULoc, osdSizeULoc;
    GLint osdScaleFactorULoc;
    GLint osdTexScaleULoc;
    GLuint osdVertexArray;
    GLuint osdVertexBuffer;
    std::map<unsigned int, GLuint> osdTextures;

    GLuint logoTexture;

    void osdRenderItem(OSDItem* item) override;
    void osdDeleteItem(OSDItem* item) override;

    void kbPreviewTextureUpload() override;
    GLuint kbPreviewTexture = 0;
};

#endif // SCREEN_H

