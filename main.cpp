#include "dialog.h"
#include "mainwindow.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
int main(int argc, char* argv[])
{
    QApplication a(argc, argv);

    QTranslator       translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString& locale : uiLanguages) {
        const QString baseName = "IM_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }

    Dialog dlg;
    if (dlg.exec() != QDialog::Accepted) { return 0; }
    MainWindow w(nullptr, dlg.m_client);
    w.show();
    return a.exec();
}
