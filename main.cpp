#include "mainwindow.h"
#include "widgets.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    if (MainWindow::alreadyRunning()) // two copies would each act on every tap
        return 0;
    applyTheme(a);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "cronos_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }
    MainWindow w;
    if (!a.arguments().contains(QStringLiteral("--hidden"))) // how Windows starts it at sign-in: tray icon only
        w.show();
    return QApplication::exec();
}
