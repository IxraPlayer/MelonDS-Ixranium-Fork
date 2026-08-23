#ifndef SETTINGSHUBDIALOG_H
#define SETTINGSHUBDIALOG_H

#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QScrollArea>

class SettingsHubDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsHubDialog(QWidget* parent);
    int addCategory(const QString& title);

    // Embeds the given widget into the right-hand panel, replacing whatever
    // page is currently shown there, and takes ownership of it. The widget
    // must not have been shown/opened as a top-level window before this is
    // called (embedding a still-modal QDialog freezes the UI).
    void setPage(QWidget* page);

signals:
    void categorySelected(int index);

private slots:
    void onItemClicked(QListWidgetItem* item);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void updateRoundedMask();

    QListWidget* sidebar;
    QStackedWidget* stack;
    QScrollArea* stackScroll;
    QWidget* placeholder;
};

#endif
