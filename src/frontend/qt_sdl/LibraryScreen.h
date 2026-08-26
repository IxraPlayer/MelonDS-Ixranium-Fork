#ifndef LIBRARYSCREEN_H
#define LIBRARYSCREEN_H

#include <QWidget>
#include <QGridLayout>
#include <QToolButton>
#include <QStringList>
#include <QMap>
#include <QIcon>
#include <QImage>
#include <QTimer>
#include <QColor>
#include <QResizeEvent>

class LibraryScreen : public QWidget
{
    Q_OBJECT

public:
    explicit LibraryScreen(QWidget* parent);

    void addGame(const QString& path);
    QStringList gamePaths() const { return paths; }

    // Decodes a plain .nds ROM's banner icon (32x32, NDS-native palette) into
    // a QImage. Shared with Window.cpp so desktop shortcuts can use the same
    // icon as the library tile. Returns a null QImage on failure (missing
    // banner, archive entries, unreadable file, etc).
    static QImage loadRomIconImage(const QString& path);

    // Reads the short game title out of the ROM's own icon/title banner
    // (first line of the English title, same text shown on the real DS
    // menu) instead of deriving a name from the filename. Returns an
    // empty string if the ROM has no readable banner (archive entries,
    // homebrew without a banner, etc) so the caller can fall back.
    static QString loadRomShortTitle(const QString& path);

    static void ApplyAccentTheme(const QString& qssThemeName);

    // Per-game console type override (0 = DS, 1 = DSi, -1 = follow the
    // global "Console type" setting). Set via each tile's right-click
    // "Details..." menu so a game can be forced to boot as DS/DSi without
    // having to open the main Emu Settings dialog and change (and later
    // revert) the global default just for that one game.
    int consoleTypeOverride(const QString& path) const;
    void setConsoleTypeOverride(const QString& path, int type);

    // Public so GameCardButton (a helper class defined alongside
    // LibraryScreen in the .cpp) can read the currently selected accent
    // hue without needing to be a friend class.
    static int AccentHueShift;

signals:
    void romActivated(QString path);
    void addGameRequested();
    void libraryChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

    // Watches every tile (including the "+" add-tile) to implement
    // press-and-drag reordering: press-drag past the OS drag threshold
    // starts a QDrag carrying the source ROM path, and dropping it onto
    // another tile reorders the library list around that drop target.
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onBgTick();

private:
    void relayout();
    QString displayName(const QString& path) const;

    QGridLayout* grid;
    QToolButton* addTile;
    QStringList paths;
    QMap<QString, QToolButton*> tiles;
    int columns;

    // Per-game console type overrides (ROM path -> 0/1), persisted via
    // QSettings under a dedicated group so it survives restarts without
    // touching the main config file's schema. Loaded once in the
    // constructor and rewritten whenever an entry changes.
    QMap<QString, int> consoleOverrides;
    void loadConsoleOverrides();
    void saveConsoleOverrides();
    void showGameDetailsDialog(const QString& path, QWidget* anchor);

    // Drag-reorder tracking: which tile the current press started on, and
    // where, so mouseMove can tell a click apart from a drag.
    QToolButton* dragCandidate = nullptr;
    QPoint dragStartPos;

    double bgHue;
    double bgPhase = 0.0;
    QTimer* bgAnimTimer;
};

#endif
