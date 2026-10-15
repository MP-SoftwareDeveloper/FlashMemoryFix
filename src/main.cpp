#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QIcon>

#include "ui/mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("FlashMemoryFix"));
    QApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app_icon.png")));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Find USB sticks / SD cards and repair, clean, partition and format them."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption dryRun(QStringLiteral("dry-run"),
                                    QStringLiteral("Show the diskpart scripts but never execute them."));
    parser.addOption(dryRun);
    parser.process(app);

    MainWindow w(parser.isSet(dryRun));
    w.show();
    return app.exec();
}
