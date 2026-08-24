#ifndef POPUPCORNERFIX_H
#define POPUPCORNERFIX_H

#include <QApplication>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QMenu>
#include <QWidget>

// QMenu popups (menu bar dropdowns and submenus) are opaque top-level
// windows. Our QSS gives them `border-radius`, but Qt fills the widget's
// full rectangle with the opaque palette color before anything is painted,
// so the four corners outside the rounded shape show up as solid black
// squares - same root cause as MainWindow/SettingsHubDialog, just for
// popups we don't construct ourselves (QMenuBar::addMenu / QMenu::addMenu
// create plain QMenu instances internally, so there's no single
// constructor to patch).
//
// QStyle::polish(QWidget*) is called on every widget right before it's
// shown, regardless of how it was created, so wrapping the app's style in
// this QProxyStyle is the one place that reaches all of them.
class PopupCornerFixStyle : public QProxyStyle
{
public:
    using QProxyStyle::QProxyStyle;

    void polish(QWidget* w) override
    {
        QProxyStyle::polish(w);
        if (qobject_cast<QMenu*>(w))
        {
            w->setAttribute(Qt::WA_TranslucentBackground);
            w->setAttribute(Qt::WA_NoSystemBackground);
        }
    }
};

// Call this right after QApplication::setStyle(...)/QApplication::setStyle(name)
// (at startup and whenever the user changes the UI theme) to keep the fix
// active. baseStyleName may be empty to just wrap whatever style is
// currently active.
inline void applyPopupCornerFix(const QString& baseStyleName = QString())
{
    QStyle* base = baseStyleName.isEmpty()
        ? QApplication::style()
        : QStyleFactory::create(baseStyleName);
    QApplication::setStyle(new PopupCornerFixStyle(base));
}

#endif // POPUPCORNERFIX_H
