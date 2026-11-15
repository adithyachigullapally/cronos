#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "tapengine.h"

#include <QMainWindow>

class LaptopView;
class Popover;
class QAbstractButton;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSystemTrayIcon;
class ZoneTile;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    void showPopover(int side); // the editor for that side's actions; -1 closes it

protected:
    void closeEvent(QCloseEvent *event) override; // hides to the tray
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *object, QEvent *event) override; // closes the popover on a click elsewhere

private:
    struct ActionRow { // what one tap pattern does
        QComboBox *kind;
        QKeySequenceEdit *keys;
        QLineEdit *command;
        QPushButton *app; // text = app name, property "target" = what to open
    };

    void load();
    void settingsChanged(); // push widget state to the engine and save it
    void onPattern(int count, float lag);
    void calibrate(); // learn which lags mean left and which mean right
    void say(const QString &line);

    ActionRow rows[2][3]; // [left, right][single, double, triple tap]
    ZoneTile *tiles[2];
    Popover *popovers[2];
    int activeSide = -1; // whose popover is open
    LaptopView *laptop;
    QComboBox *mic;
    QAbstractButton *enabled;
    QAbstractButton *ignoreInput;
    QAbstractButton *activity; // shows/hides the log
    QSlider *sensitivity;
    QLabel *sideStatus;
    QPushButton *calibrateButton;
    QPlainTextEdit *log;
    QSystemTrayIcon *tray;

    // Which lags are "left": above sideCenter if leftFirst, below it otherwise.
    // Channel 1 being the left mic is only a convention, hence calibrate().
    float sideCenter = 0;
    bool leftFirst = true;
    bool calibrated = false;
    bool calibrating = false; // taps feed calibrate(), actions are held back

    TapEngine engine; // last member: its thread stops before anything above goes away
};
#endif // MAINWINDOW_H
