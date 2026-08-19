// omavid — a dead-simple video trimmer and merger. Qt Quick (QML) UI, ffmpeg cuts.

#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QUrl>

#include "backend.h"
#include "thumbprovider.h"

int main(int argc, char *argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("omavid");

    // Associates the window with omavid.desktop so the compositor (Wayland app_id
    // = this name) and taskbars pick up our installed icon.
    app.setDesktopFileName("omavid");
    app.setWindowIcon(QIcon::fromTheme("omavid"));

    // Modern, themeable controls (the same family Quickshell builds on).
    QQuickStyle::setStyle("Material");

    auto *provider = new ThumbProvider();
    Backend backend(provider, &app);

    QQmlApplicationEngine engine;

    // The engine takes ownership of the image provider.
    engine.addImageProvider("thumbs", provider);

    engine.rootContext()->setContextProperty("backend", &backend);

    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return -1;

    // Optionally open files passed on the command line.
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i)
        backend.load(QUrl::fromLocalFile(args.at(i)));

    return app.exec();
}
