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

    // Drag-reorder tracking: which tile the current press started on, and
    // where, so mouseMove can tell a click apart from a drag.
    QToolButton* dragCandidate = nullptr;
    QPoint dragStartPos;

    double bgHue;
    double bgPhase = 0.0;
    QTimer* bgAnimTimer;
};

#endif
