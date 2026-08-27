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

    static QImage loadRomIconImage(const QString& path);
    static QString loadRomShortTitle(const QString& path);
    static void ApplyAccentTheme(const QString& qssThemeName);

    int consoleTypeOverride(const QString& path) const;
    void setConsoleTypeOverride(const QString& path, int type);

    QString controlSchemeOverride(const QString& path) const;
    void setControlSchemeOverride(const QString& path, const QString& schemeName);

    static int AccentHueShift;

signals:
    void romActivated(QString path);
    void addGameRequested();
    void libraryChanged();

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
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

    QMap<QString, int> consoleOverrides;
    void loadConsoleOverrides();
    void saveConsoleOverrides();

    QMap<QString, QString> schemeOverrides;
    void loadSchemeOverrides();
    void saveSchemeOverrides();

    void showGameDetailsDialog(const QString& path, QWidget* anchor);

    QToolButton* dragCandidate = nullptr;
    QPoint dragStartPos;

    double bgHue;
    double bgPhase = 0.0;
    QTimer* bgAnimTimer;

    QPixmap vignetteCache;
    QSize vignetteCacheSize;
    void rebuildVignetteCache(const QRectF& r, const QPainterPath& path);
};

#endif
