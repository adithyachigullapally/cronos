// Checks when the side popovers open and close, by sending clicks straight to
// the widgets. The window is never shown and the real settings file is not
// touched. Run: ctest, or ui_test.exe
// With a folder argument it also saves pictures of the window there
// (main.png, left.png, right.png), for looking at a UI change.
#include "mainwindow.h"
#include "widgets.h"

#include <QLabel>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>

static void click(QWidget *target, QPoint at)
{
    QMouseEvent press(QEvent::MouseButtonPress, at, target->mapToGlobal(at), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QMouseEvent release(QEvent::MouseButtonRelease, at, target->mapToGlobal(at), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &release);
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    applyTheme(a);
    QTemporaryDir settings; // start from defaults, with the microphone off
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    QSettings(QSettings::IniFormat, QSettings::UserScope, "cronos", "cronos").setValue("enabled", false);

    MainWindow w;
    const QPixmap picture = w.grab(); // also lays the window out, which placing a popover relies on
    if (argc > 1) {
        const QString folder = QString::fromLocal8Bit(argv[1]);
        picture.save(folder + "/main.png");
        // ponytail: one 256 px image, Windows scales it down for small sizes; add 16/32/48 if those look soft.
        w.windowIcon().pixmap(256, 256).save(folder + "/cronos.ico");
        w.showPopover(0);
        w.grab().save(folder + "/left.png");
        w.showPopover(1);
        w.grab().save(folder + "/right.png");
        w.showPopover(-1);
    }

    QWidget *tile[2] = {}, *pop[2] = {}, *inLeftPop = nullptr, *title = nullptr, *laptop = nullptr;
    for (QAbstractButton *b : w.findChildren<QAbstractButton *>()) {
        if (b->accessibleName() == "Left side actions") tile[0] = b;
        if (b->accessibleName() == "Right side actions") tile[1] = b;
    }
    for (QLabel *l : w.findChildren<QLabel *>()) {
        if (l->objectName() == "title") title = l;
        if (l->objectName() == "popTitle") {
            pop[l->text() == "Left side" ? 0 : 1] = l->parentWidget();
            if (l->text() == "Left side") inLeftPop = l;
        }
    }
    for (QWidget *c : w.findChildren<QWidget *>())
        if (dynamic_cast<LaptopView *>(c)) laptop = c;
    if (!tile[0] || !tile[1] || !pop[0] || !pop[1] || !title || !laptop) {
        std::printf("FAIL: could not find the widgets\n");
        return 1;
    }

    int failed = 0;
    auto expect = [&](const char *step, bool left, bool right) {
        if (pop[0]->isHidden() == left || pop[1]->isHidden() == right) {
            std::printf("FAIL: %s\n", step);
            ++failed;
        }
    };
    const QPoint leftHalf(laptop->width() / 4, laptop->height() - 40), rightHalf(laptop->width() * 3 / 4, laptop->height() - 40);
    expect("both closed at start", false, false);
    click(tile[0], {20, 20});
    expect("clicking the left tile opens the left popover", true, false);
    click(inLeftPop, {5, 5});
    expect("clicking inside the popover keeps it open", true, false);
    click(tile[0], {20, 20});
    expect("clicking the left tile again closes it", false, false);
    click(tile[0], {20, 20});
    click(tile[1], {20, 20});
    expect("clicking the right tile swaps to the right popover", false, true);
    click(title, {5, 5});
    expect("clicking elsewhere closes it", false, false);
    click(laptop, leftHalf);
    expect("clicking the left half of the deck opens the left popover", true, false);
    click(laptop, rightHalf);
    expect("clicking the right half of the deck opens the right popover", false, true);
    click(laptop, {laptop->width() / 2, 40});
    expect("clicking the lid closes it", false, false);
    click(tile[1], {20, 20});
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(tile[1], &escape);
    expect("Escape closes it", false, false);

    if (!failed)
        std::printf("ui_test: all passed\n");
    return failed;
}
