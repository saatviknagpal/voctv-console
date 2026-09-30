#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include "core/Types.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("VOCTV Console");
    voctv::registerMetaTypes();
    applyDarkTheme(app);

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption shotOpt("screenshot", "Run the scripted demo, save a screenshot to <file>, then exit.", "file");
    QCommandLineOption framesOpt("capture-frames", "Run the scripted demo, save PNG frames into <dir>, then exit.", "dir");
    parser.addOptions({shotOpt, framesOpt});
    parser.process(app);

    MainWindow window;
    window.resize(1440, 900);
    window.show();

    if (parser.isSet(shotOpt) || parser.isSet(framesOpt)) {
        window.setHeadless(true);
        auto grab = [&window] {
            // Grab from the root window so the native OpenGL 3D view is captured too.
            const QRect g = window.frameGeometry();
            return window.screen()->grabWindow(0, g.x(), g.y(), g.width(), g.height());
        };
        auto *capture = new QTimer(&window);
        int frameNo = 0;
        const QString dir = parser.value(framesOpt);
        if (!dir.isEmpty()) {
            QDir().mkpath(dir);
            QObject::connect(capture, &QTimer::timeout, &window, [&, dir] {
                grab().save(QStringLiteral("%1/frame_%2.png").arg(dir).arg(frameNo++, 4, 10, QChar('0')));
            });
            capture->start(100);
        }
        window.runDemo([&] {
            capture->stop();
            int code = 0;
            if (parser.isSet(shotOpt)) {
                const QPixmap shot = grab();
                if (shot.isNull() || !shot.save(parser.value(shotOpt))) {
                    qWarning("Could not save screenshot to %s (null=%d %dx%d)", qPrintable(parser.value(shotOpt)), shot.isNull(), shot.width(), shot.height());
                    code = 1;
                }
            }
            QTimer::singleShot(200, &app, [&app, code] { app.exit(code); });
        });
    }
    return app.exec();
}
