#include <QApplication>
#include <QStyleFactory>
#include <QFile>
#include <QDir>
#include <QFont>
#include <QFileInfo>
#include "MainWindow.h"

static QString findStylesheet()
{
    QString bundle = QCoreApplication::applicationDirPath()
                     + "/../Resources/styles/style.qss";
    if (QFileInfo::exists(bundle)) return bundle;
    QString build = QCoreApplication::applicationDirPath()
                    + "/styles/style.qss";
    if (QFileInfo::exists(build)) return build;
    QString src = QDir::currentPath() + "/resources/styles/style.qss";
    if (QFileInfo::exists(src)) return src;
    return {};
}

int main(int argc, char *argv[])
{
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

    QApplication app(argc, argv);
    app.setApplicationName("7-Zip");
    app.setOrganizationName("7-Zip");
    app.setApplicationVersion("1.0.0");
    app.setStyle(QStyleFactory::create("Fusion"));

    QFont sysFont = app.font();
    sysFont.setPointSize(12);
    app.setFont(sysFont);

    QString qssPath = findStylesheet();
    if (!qssPath.isEmpty()) {
        QFile f(qssPath);
        if (f.open(QFile::ReadOnly)) {
            app.setStyleSheet(f.readAll());
            f.close();
        }
    }

    MainWindow window;
    window.show();

    // Handle files passed as command-line arguments
    QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString &arg = args.at(i);
        if (!arg.startsWith('-') && QFileInfo::exists(arg)) {
            window.loadArchive(arg);
            break;
        }
    }

    return app.exec();
}
