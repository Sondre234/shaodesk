// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.hpp"
#include "controller.hpp"
#include "system_status.hpp"
#include "view.hpp"
#include <QAbstractItemModel>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJSValue>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <functional>
#include <iostream>

namespace {
// Records what the panel asks of the sound server.
class FakeAudio : public Audio {
  public:
    QStringList requests;

  protected:
    void sendVolume(const QString &output, int percent) override {
        requests << QString("volume %1 %2").arg(output).arg(percent);
    }
    void sendMute(const QString &output, bool muted) override {
        requests << QString("mute %1 %2").arg(output).arg(muted);
    }
    void sendOutput(const QString &output, const std::vector<uint32_t> &streams) override {
        requests << QString("output %1 %2").arg(output).arg(streams.size());
    }
    void sendStreamVolume(uint32_t id, int percent) override {
        requests << QString("stream %1 %2").arg(id).arg(percent);
    }
    void sendStreamMute(uint32_t id, bool muted) override {
        requests << QString("stream-mute %1 %2").arg(id).arg(muted);
    }
};
} // namespace
#ifdef __SANITIZE_ADDRESS__
// The QML objects a test script creates are still reachable to Qt at exit, not to LeakSanitizer.
extern "C" const char *__asan_default_options() { return "detect_leaks=0"; }
#endif

static int fail(const char *why) {
    std::cerr << why << '\n';
    return 1;
}

int main(int argc, char **argv) {
    // A named screen, as compositor outputs are: the workspace indicator is keyed by it.
    QTemporaryDir screens;
    QFile layout(screens.filePath("screens.json"));
    if (!screens.isValid() || !layout.open(QIODevice::WriteOnly) ||
        layout.write(R"({"screens": [{"name": "TEST-1", "x": 0, "y": 0, "width": 1280,
                         "height": 720, "logicalDpi": 96, "logicalBaseDpi": 96, "dpr": 1}]})") < 0)
        return fail("could not write the screens layout");
    layout.close();
    qputenv("QT_QPA_PLATFORM", ("offscreen:configfile=" + layout.fileName()).toLocal8Bit());
    // One installed application, found by its StartupWMClass, and a private pin store. GLib
    // caches these directories on first use, so they are set before anything starts.
    QDir(screens.path()).mkpath("data/applications");
    QFile desktopFile(screens.filePath("data/applications/shaodesk-test-app.desktop"));
    if (!desktopFile.open(QIODevice::WriteOnly) ||
        desktopFile.write("[Desktop Entry]\nType=Application\nName=Fake app\nExec=true\n"
                          "StartupWMClass=Fake\n") < 0)
        return fail("could not write the fake application");
    desktopFile.close();
    QFile otherFile(screens.filePath("data/applications/shaodesk-test-other.desktop"));
    if (!otherFile.open(QIODevice::WriteOnly) ||
        otherFile.write("[Desktop Entry]\nType=Application\nName=Other app\nExec=true\n") < 0)
        return fail("could not write the other application");
    otherFile.close();
    qputenv("XDG_DATA_HOME", screens.filePath("data").toLocal8Bit());
    qputenv("XDG_DATA_DIRS", screens.filePath("none").toLocal8Bit());
    qputenv("XDG_STATE_HOME", screens.filePath("state").toLocal8Bit());
    qputenv("XDG_CACHE_HOME", screens.filePath("cache").toLocal8Bit());
    const auto pins = screens.filePath("state/shaodesk/pinned");
    QGuiApplication app(argc, argv);
    if (argc != 2)
        return fail("usage: shell_ui_test CMAKE (run by the launcher as CMAKE -E touch FILE)");
    QTemporaryDir directory;
    if (!directory.isValid())
        return fail("no temporary directory");
    auto config = directory.filePath("init.lua");
    auto marker = directory.filePath("launched");
    // An application with desktop actions: one that runs, and one whose program is missing. It
    // is written before the controller first reads the applications.
    const auto actionMarker = directory.filePath("action");
    QFile actionsFile(screens.filePath("data/applications/shaodesk-test-actions.desktop"));
    if (!actionsFile.open(QIODevice::WriteOnly) ||
        actionsFile.write(QString("[Desktop Entry]\nType=Application\nName=Action app\nExec=true\n"
                                  "GenericName=File toucher\nKeywords=stamp;mark;\n"
                                  "Comment=Leaves a file behind\n"
                                  "Actions=touch;missing;\n\n"
                                  "[Desktop Action touch]\nName=Touch a file\nIcon=document-new\n"
                                  "Exec=\"%1\" -E touch \"%2\"\n\n"
                                  "[Desktop Action missing]\nName=Missing program\n"
                                  "Exec=/nonexistent/shaodesk-missing-program\n")
                              .arg(QString::fromLocal8Bit(argv[1]), actionMarker)
                              .toUtf8()) < 0)
        return fail("could not write the application with actions");
    actionsFile.close();
    // Two pictures for the wallpaper picker, in two subfolders.
    const auto walls = directory.filePath("walls");
    for (const auto *name : {"a/one.png", "b/two.png"}) {
        QDir(walls).mkpath(QFileInfo(name).path());
        QImage picture(64, 36, QImage::Format_RGB32);
        picture.fill(Qt::darkCyan);
        if (!picture.save(walls + "/" + name))
            return fail("could not save a wallpaper");
    }
    QFile file(config);
    if (!file.open(QIODevice::WriteOnly))
        return fail("could not write the configuration");
    // Long Lua strings preserve paths without shell interpolation. The widgets Quick Settings
    // holds by default are on the bar, where most of this test uses them.
    const QString barWidgets = "widgets={network='bar',battery='bar',volume='bar',tiling='bar',profiles='bar'},";
    const auto lua = QString("return {layout={workspace_names={'web','','','mail'}},"
                             "power={countdown=2},"
                             "profile='dark',profiles={dark={},light={shell={accent='#336699'}}},"
                             "shell={wallpaper='walls/a/one.png'," + barWidgets + "wallpapers=[[%3]],"
                             "launchers={{name='Test app',command={[[%1]],'-E','touch',[[%2]]}}}}}")
                         .arg(QString::fromLocal8Bit(argv[1]), marker, walls);
    file.write(lua.toUtf8());
    file.close();
    // A stand-in for the compositor's control socket, with this screen as its only output.
    // Tiling is per output; the focused one it reports first is always the opposite of this
    // screen's, as if another monitor had focus, so the panel must show its own.
    const auto output = app.primaryScreen()->name();
    QString urgentLines; // what a state says about windows asking for attention
    auto state = [&output, &urgentLines](bool tiling, int workspace) {
        return (QString("tiling %1\nworkspace %2\noutput %3 %2 1,2 %4\n")
                    .arg(tiling ? "off" : "on")
                    .arg(workspace)
                    .arg(output)
                    .arg(tiling ? "on" : "off") +
                urgentLines)
            .toUtf8();
    };
    QLocalServer compositor;
    QLocalSocket *subscriber = nullptr;
    bool toggled = false;
    int currentWorkspace = 2;
    QStringList switches, requests;
    int layoutSwitches = 0; // "switch_layout next" requests, answered with the second layout
    bool nightLight = false; // what "night_light_toggle" turns, and the state it then announces
    bool holdSessions = false;
    QString powerRefusal; // what the compositor answers a power action with; "" for ok
    QLocalSocket *pendingSessions = nullptr;
    // What the compositor does with "profile NAME": saves it and reloads the shell.
    std::function<void(const QString &)> pickProfile;
    QObject::connect(&compositor, &QLocalServer::newConnection, [&] {
        auto *client = compositor.nextPendingConnection();
        QObject::connect(client, &QLocalSocket::readyRead, [&, client] {
            if (!client->canReadLine())
                return;
            auto request = client->readLine();
            if (request == "subscribe\n") {
                subscriber = client;
                client->write("ok\n" + state(false, 2));
            } else if (request == "output " + output.toUtf8() + " toggle_tiling\n") {
                toggled = !toggled;
                client->write("ok\n");
                client->disconnectFromServer();
                subscriber->write(state(toggled, currentWorkspace));
            } else if (request.startsWith("profile ")) {
                requests.push_back(QString::fromUtf8(request).trimmed());
                client->write("ok\n");
                client->disconnectFromServer();
                pickProfile(QString::fromUtf8(request).trimmed().section(' ', 1));
            } else if (request == "night_light_toggle\n") {
                requests.push_back("night_light_toggle");
                nightLight = !nightLight;
                client->write("ok\n");
                client->disconnectFromServer();
                subscriber->write(nightLight ? "night-light on on\n" : "night-light off off\n");
            } else if (request == "switch_layout next\n") {
                ++layoutSwitches;
                client->write("ok\n");
                client->disconnectFromServer();
                subscriber->write("keyboard-layout 2 2 no Norwegian\n");
            } else if (QStringList{"lock", "suspend", "hibernate", "reboot", "poweroff", "logout"}
                           .contains(QString::fromUtf8(request).trimmed())) {
                requests.push_back(QString::fromUtf8(request).trimmed());
                client->write(powerRefusal.isEmpty() ? QByteArray("ok\n")
                                                     : ("error: " + powerRefusal + "\n").toUtf8());
                client->disconnectFromServer();
            } else if (request == "session list\n") {
                if (holdSessions) { // answered later, by the test
                    pendingSessions = client;
                    return;
                }
                client->write("ok\nwork\t3\t1700000000\n");
                client->disconnectFromServer();
            } else if (request.startsWith("output ") || request.startsWith("session ") ||
                       request == "toggle_tiling\n" || request == "layout_monocle\n" ||
                       request == "terminal\n") {
                if (!request.startsWith("output ")) {
                    requests.push_back(QString::fromUtf8(request).trimmed());
                    client->write("ok\n");
                    client->disconnectFromServer();
                    return;
                }
                switches.push_back(QString::fromUtf8(request).trimmed());
                client->write("ok\n");
                client->disconnectFromServer();
                currentWorkspace = request.trimmed().split(' ').last().toInt();
                subscriber->write(state(toggled, currentWorkspace));
            }
        });
    });
    if (!compositor.listen(directory.filePath("control.sock")))
        return fail("could not listen on the fake control socket");
    qputenv("SHAODESK_SOCKET", compositor.fullServerName().toLocal8Bit());
    ShellController controller(config.toStdString());
    pickProfile = [&controller](const QString &name) {
        shaodesk::save_profile(name.toStdString());
        controller.reload();
    };
    // A stand-in sound server for the volume control, made before the panel so that it outlives
    // it: what reads it never finds it gone.
    FakeAudio audio;
    ShellView view(controller, app.primaryScreen(), false, true);
    if (view.status() != QQuickView::Ready) {
        for (const auto &error : view.errors())
            std::cerr << error.toString().toStdString() << '\n';
        return 1;
    }
    // An application picks its own app ID: a path in it is no icon to load from disk.
    if (controller.iconFor("/etc/hostname") != "application-x-executable" ||
        controller.iconFor("../../x") != "application-x-executable" ||
        controller.iconFor("org.example.App") != "org.example.App") {
        std::cerr << "a window's app ID can name a file for the icon\n";
        return 1;
    }
    // An application's desktop actions are listed in its entry's order with their icons, and run
    // as the application is started; a failure, or an action that is not there, shows across the
    // panel.
    {
        const auto actions = controller.appActions("shaodesk-test-actions.desktop");
        if (actions.size() != 2 || actions[0].toMap()["action"] != "touch" ||
            actions[0].toMap()["name"] != "Touch a file" ||
            actions[0].toMap()["icon"] != "document-new" ||
            actions[1].toMap()["action"] != "missing" || actions[1].toMap()["icon"] != "" ||
            !controller.appActions("shaodesk-test-app.desktop").isEmpty() ||
            !controller.appActions("pinned:0").isEmpty() ||
            !controller.appActions("not-installed.desktop").isEmpty()) {
            std::cerr << "the desktop actions are not listed as the entries give them\n";
            return 1;
        }
        if (!controller.launchAction("shaodesk-test-actions.desktop", "touch") ||
            !QTest::qWaitFor([&] { return QFile::exists(actionMarker); }) ||
            !controller.error().isEmpty()) {
            std::cerr << "a desktop action did not run: " << controller.error().toStdString() << '\n';
            return 1;
        }
        // The launch is remembered in the state directory, for the start menu's recent list.
        QFile launches(screens.filePath("state/shaodesk/launches"));
        const auto recent = controller.startMenu()->recent();
        if (!launches.open(QIODevice::ReadOnly) ||
            !launches.readAll().startsWith("shaodesk-test-actions.desktop\t1\t") || recent.size() != 1 ||
            recent[0].toMap()["appId"] != "shaodesk-test-actions.desktop")
            return fail("running a desktop action was not recorded as a launch");
        if (controller.launchAction("shaodesk-test-actions.desktop", "missing") ||
            !controller.error().startsWith("Could not launch Action app: ")) {
            std::cerr << "a desktop action whose program is missing was not reported: "
                      << controller.error().toStdString() << '\n';
            return 1;
        }
        if (controller.launchAction("shaodesk-test-actions.desktop", "absent") ||
            controller.error() != "This action is no longer available.") {
            std::cerr << "an action the entry does not have was not refused\n";
            return 1;
        }
        controller.clearError();
    }
    // An installed application's record says what else a search finds it by.
    {
        QVariantMap record;
        for (const auto &app : controller.apps())
            if (app.toMap()["appId"] == "shaodesk-test-actions.desktop")
                record = app.toMap();
        if (record["genericName"] != "File toucher" ||
            record["keywords"].toStringList() != QStringList{"stamp", "mark"} ||
            record["description"] != "Leaves a file behind")
            return fail("an application's generic name, keywords and comment are not in its record");
    }
    // Applications installed or removed while the shell runs are found without being asked.
    {
        auto installed = [&](const QString &id) {
            const auto apps = controller.apps();
            return std::any_of(apps.begin(), apps.end(),
                               [&](const QVariant &app) { return app.toMap()["appId"] == id; });
        };
        QFile later(screens.filePath("data/applications/shaodesk-test-later.desktop"));
        if (!later.open(QIODevice::WriteOnly) ||
            later.write("[Desktop Entry]\nType=Application\nName=Later app\nExec=true\n") < 0)
            return fail("could not write an application to install");
        later.close();
        if (!QTest::qWaitFor([&] { return installed("shaodesk-test-later.desktop"); }, 10000))
            return fail("an application installed while the shell ran was not found");
        later.remove();
        if (!QTest::qWaitFor([&] { return !installed("shaodesk-test-later.desktop"); }, 10000))
            return fail("an application removed while the shell ran stayed listed");
    }
    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        return fail("the panel never showed");
    // The popups' window, as large as the output, with the bar's along its bottom edge.
    PopoverWindow *popover = view.popover();
    if (!popover || popover->size() != ShellView::previewSize() || popover->isVisible())
        return fail("the panel has no popover the size of its output, or it shows at start");
    // Loader items, Repeater items and the like are found through the item tree rather than as
    // QObject children.
    std::function<QQuickItem *(QQuickItem *, const QString &)> find =
        [&](QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name)
            return item;
        for (auto *child : item->childItems())
            if (auto *found = find(child, name))
                return found;
        // The panel's popups are in the popover's window.
        return item == view.rootObject() ? find(popover->contentItem(), name) : nullptr;
    };
    // Whether a popup is open, done sliding in, and lies inside the popover, off the bar along its
    // bottom edge, the bar's own surface keeping its size. A menu's first card stands for it.
    auto inPopover = [&](QQuickItem *popup) {
        if (auto *card = popup->property("card").value<QQuickItem *>())
            popup = card;
        const QRectF area = popup->mapRectToScene(QRectF(0, 0, popup->width(), popup->height()));
        const QVariant progress = popup->property("progress");
        return popup->isVisible() && (!progress.isValid() || progress.toReal() == 1) &&
               popup->window() == popover && popover->isVisible() &&
               area.top() >= 0 && area.left() >= 0 &&
               area.bottom() <= popover->height() - view.height() &&
               area.right() <= popover->width() && view.height() == controller.panelExtent();
    };
    // The popups are made on first use, not with the panel.
    for (const char *popup : {"audioMixer", "calendar", "audioOutputs", "contextMenu", "groupList",
                              "applicationSearch"})
        if (find(view.rootObject(), popup)) {
            std::cerr << popup << " was made before it was needed\n";
            return 1;
        }
    const QPoint start(30, controller.panelHeight() / 2);
    QTest::mouseMove(&view, start);
    QTest::qWait(200); // Hover first: a tooltip must not swallow the following press.
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, start);
    if (!QTest::qWaitFor([&] { return view.rootObject()->property("launcherOpen").toBool(); })) {
        std::cerr << "hover then click did not open the launcher\n";
        return 1;
    }
    auto *search = find(view.rootObject(), "applicationSearch");
    if (!search || !QTest::qWaitFor([&] { return search->hasActiveFocus(); })) {
        std::cerr << "launcher search did not receive keyboard focus\n";
        return 1;
    }
    for (Qt::Key key : {Qt::Key_T, Qt::Key_E, Qt::Key_S, Qt::Key_T})
        QTest::keyClick(search->window(), key);
    QTest::keyClick(search->window(), Qt::Key_Return);
    if (!QTest::qWaitFor([&] { return QFile::exists(marker); })) {
        std::cerr << "search and Enter did not launch the configured command\n";
        return 1;
    }
    if (view.rootObject()->property("launcherOpen").toBool()) {
        std::cerr << "launcher remained open after launching\n";
        return 1;
    }
    // A moment after startup the popups are made ahead of their first use.
    view.rootObject()->setProperty("warm", true);
    if (!QTest::qWaitFor([&] {
            for (const char *popup : {"audioMixer", "calendar", "audioOutputs", "contextMenu", "groupList"})
                if (!find(view.rootObject(), popup))
                    return false;
            return true;
        })) {
        std::cerr << "the popups were not made ahead of use\n";
        return 1;
    }
    auto panelTiling = [&view] { return view.rootObject()->property("tiling").toBool(); };
    auto *tiling = view.rootObject()->findChild<QQuickItem *>("tilingToggle");
    if (!tiling || !QTest::qWaitFor([&] { return controller.tilingAvailable(); }) ||
        panelTiling()) {
        std::cerr << "tiling state did not arrive from the control socket\n";
        return 1;
    }
    const QPoint toggle =
        tiling->mapToScene(QPointF(tiling->width() / 2, tiling->height() / 2)).toPoint();
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, toggle);
    if (!QTest::qWaitFor([&] { return toggled && panelTiling(); })) {
        std::cerr << "the tiling button did not toggle tiling\n";
        return 1;
    }
    // The workspace indicator shows this output's state and switches it.
    auto workspace = [&](int number) {
        return find(view.rootObject(), QString("workspace%1").arg(number));
    };
    if (!workspace(4) || workspace(5) || !workspace(2)->property("current").toBool() ||
        workspace(1)->property("current").toBool() ||
        !workspace(1)->property("occupied").toBool() ||
        workspace(3)->property("occupied").toBool()) {
        std::cerr << "the workspace indicator does not show the output's workspaces\n";
        return 1;
    }
    if (workspace(1)->property("label").toString() != "web" ||
        workspace(4)->property("label").toString() != "mail" ||
        !workspace(2)->property("label").toString().isEmpty() ||
        workspace(1)->width() <= workspace(2)->width()) {
        std::cerr << "the workspace indicator does not show workspace names\n";
        return 1;
    }
    auto centre = [&](QQuickItem *item) {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    };
    // A click in the middle of an item, in the window it is in.
    auto click = [&](QQuickItem *item, Qt::MouseButton button = Qt::LeftButton, int delay = -1) {
        QTest::mouseClick(item->window(), button, Qt::NoModifier, centre(item), delay);
    };
    click(workspace(3));
    if (!QTest::qWaitFor([&] { return workspace(3)->property("current").toBool(); }) ||
        switches != QStringList{"output " + output + " workspace 3"}) {
        std::cerr << "clicking a workspace did not switch to it\n";
        return 1;
    }
    // The current workspace's pill slides over to it, and takes its width.
    {
        auto *pill = find(view.rootObject(), "workspacePill");
        if (!pill || !QTest::qWaitFor([&] {
                return pill->isVisible() && pill->x() == workspace(3)->x() &&
                       pill->width() == workspace(3)->width();
            }))
            return fail("the current workspace's pill did not settle under it");
    }
    auto scrollAt = [&](QPoint at, int delta) {
        QWheelEvent event(at, view.mapToGlobal(at), QPoint(), QPoint(0, delta), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&view, &event);
    };
    auto scroll = [&](int delta) { scrollAt(centre(workspace(2)), delta); };
    scroll(-120); // down: the next workspace
    if (!QTest::qWaitFor([&] { return workspace(4)->property("current").toBool(); })) {
        std::cerr << "scrolling down did not page to the next workspace\n";
        return 1;
    }
    scroll(-120); // already on the last one
    scroll(60);   // half a notch does nothing yet
    scroll(60);
    if (!QTest::qWaitFor([&] { return workspace(3)->property("current").toBool(); }) ||
        switches.size() != 3 || switches.last() != "output " + output + " workspace 3") {
        std::cerr << "scrolling up did not page back one workspace: "
                  << switches.join(", ").toStdString() << '\n';
        return 1;
    }
    // The bar's empty space, left of its first button, pages workspaces too.
    auto *bar = find(view.rootObject(), "bar");
    scrollAt(bar->mapToScene(QPointF(3, bar->height() / 2)).toPoint(), -120);
    if (!QTest::qWaitFor([&] { return workspace(4)->property("current").toBool(); }) ||
        switches.size() != 4 || switches.last() != "output " + output + " workspace 4") {
        std::cerr << "scrolling the bar's empty space did not page workspaces\n";
        return 1;
    }
    // A window asking for attention marks its workspace in the indicator (and is listed); when
    // it is done, the marks go.
    if (controller.urgentCount() != 0 || workspace(3)->property("urgent").toBool()) {
        std::cerr << "a workspace is urgent before anything asked\n";
        return 1;
    }
    urgentLines = "urgent 2\nurgent-output " + output + " 1,3\n"
                  "urgent-window " + output + "\t3\tfake\tFake\n"
                  "urgent-window " + output + "\t1\t\tNo app id\n";
    subscriber->write(state(toggled, currentWorkspace));
    if (!QTest::qWaitFor([&] { return controller.urgentCount() == 2; }) ||
        !workspace(3)->property("urgent").toBool() || !workspace(1)->property("urgent").toBool() ||
        workspace(2)->property("urgent").toBool() || workspace(4)->property("urgent").toBool()) {
        std::cerr << "the workspace indicator does not mark workspaces with urgent windows\n";
        return 1;
    }
    const auto asking = controller.urgentWindows();
    if (asking.size() != 2 || asking[0].toMap()["appId"].toString() != "fake" ||
        asking[0].toMap()["title"].toString() != "Fake" ||
        asking[0].toMap()["workspace"].toInt() != 3 || !asking[1].toMap()["appId"].toString().isEmpty() ||
        asking[1].toMap()["title"].toString() != "No app id") {
        std::cerr << "the urgent windows were not parsed in order\n";
        return 1;
    }
    if (!find(workspace(3), "workspaceUrgent3") || !find(workspace(3), "workspaceUrgent3")->isVisible() ||
        find(workspace(2), "workspaceUrgent2")->isVisible()) {
        std::cerr << "the urgent marker of a workspace is not shown where it should be\n";
        return 1;
    }
    urgentLines = "urgent 0\n";
    subscriber->write(state(toggled, currentWorkspace));
    if (!QTest::qWaitFor([&] { return controller.urgentCount() == 0; }) ||
        workspace(3)->property("urgent").toBool() || workspace(1)->property("urgent").toBool() ||
        !controller.urgentWindows().isEmpty()) {
        std::cerr << "the workspace marks did not go when the windows stopped asking\n";
        return 1;
    }
    auto rewrite = [&config](const QString &source) {
        QFile again(config);
        return again.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
               again.write(source.toUtf8()) >= 0;
    };
    // Slows the shell's animations down to a quarter of their speed, or brings them back, so that
    // a test sees what moves on its way.
    auto slowMotion = [&](bool slow) {
        if (!rewrite(slow ? QString(lua).replace("return {", "return {animations={speed=0.25},") : lua))
            return false;
        controller.reload();
        return QTest::qWaitFor([&] { return controller.animationSpeed() == (slow ? 0.25 : 1); });
    };
    // shell.workspaces_shown = 3 shows the current workspace with its neighbours, the last
    // three on the last one, and the names follow their workspaces.
    if (!rewrite(QString(lua).replace("shell={", "shell={workspaces_shown=3,")))
        return fail("could not rewrite the configuration");
    controller.reload();
    if (!QTest::qWaitFor([&] { return controller.workspacesShown() == 3 && !workspace(1); }) ||
        !workspace(2) || !workspace(3) || !workspace(4) || !workspace(4)->property("current").toBool() ||
        workspace(4)->property("label").toString() != "mail") {
        std::cerr << "shell.workspaces_shown = 3 did not show the last three workspaces\n";
        return 1;
    }
    // The bar's layout moves the narrower indicator on its next polish, which a grab runs.
    view.grabWindow();
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, centre(workspace(2)));
    if (!QTest::qWaitFor([&] { return workspace(1) && !workspace(4); }) ||
        !workspace(2)->property("current").toBool() || !workspace(3) ||
        workspace(1)->property("label").toString() != "web") {
        std::cerr << "shell.workspaces_shown = 3 did not show the workspaces around the current one\n";
        for (int n = 1; n <= 4; ++n)
        return 1;
    }
    if (!rewrite(lua))
        return fail("could not restore the configuration");
    controller.reload();
    if (!QTest::qWaitFor([&] { return workspace(4) != nullptr; }) || !workspace(1))
        return fail("the workspace indicator did not show every workspace again");
    view.grabWindow();
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, centre(workspace(4)));
    if (!QTest::qWaitFor([&] { return workspace(4)->property("current").toBool(); }))
        return fail("could not switch back to the last workspace");
    // The window switcher's list says which windows are asking for attention (an older
    // five-field line means none).
    subscriber->write(("switcher " + output + " 0 3\n"
                       "switcher-window fake\tFake\t" + output + "\t3\t0\t1\n"
                       "switcher-window \tNo app id\t" + output + "\t1\t1\t0\n"
                       "switcher-window old\tOld\t" + output + "\t1\t0\n").toUtf8());
    if (!QTest::qWaitFor([&] { return controller.switcherWindows().size() == 3; })) {
        std::cerr << "the switcher's windows were not parsed\n";
        return 1;
    }
    const auto listed = controller.switcherWindows();
    if (!listed[0].toMap()["urgent"].toBool() || listed[1].toMap()["urgent"].toBool() ||
        listed[2].toMap()["urgent"].toBool() || !listed[1].toMap()["minimized"].toBool() ||
        !listed[1].toMap()["appId"].toString().isEmpty()) {
        std::cerr << "the switcher does not say which windows are urgent\n";
        return 1;
    }
    subscriber->write("switcher-close\n");
    if (!QTest::qWaitFor([&] { return controller.switcherWindows().isEmpty(); })) {
        std::cerr << "the switcher did not close\n";
        return 1;
    }
    // The switcher, the overview and the command palette opening on this output close what is
    // open here: the popover would be over them.
    {
        auto openLauncher = [&] {
            view.rootObject()->setProperty("launcherOpen", true);
            return QTest::qWaitFor([&] { return popover->isVisible(); });
        };
        auto closed = [&] {
            return QTest::qWaitFor([&] { return !view.rootObject()->property("menuOpen").toBool(); });
        };
        if (!openLauncher())
            return fail("the launcher did not open");
        subscriber->write(("switcher " + output + " 0 0\n").toUtf8());
        if (!closed())
            return fail("the window switcher opening did not close the launcher");
        subscriber->write("switcher-close\n");
        if (!openLauncher())
            return fail("the launcher did not open again");
        subscriber->write(("overview " + output + " 0 0 1 0 0 0 1100 668 -\n").toUtf8());
        if (!closed())
            return fail("the overview opening did not close the launcher");
        subscriber->write("overview-close\n");
        if (!openLauncher())
            return fail("the launcher did not open a third time");
        controller.palette()->open(output);
        if (!closed())
            return fail("the command palette opening did not close the launcher");
        controller.palette()->close();
    }
    // The keyboard layout indicator: the active layout's short name while there are two or
    // more; clicking it asks for the next; shell.widgets.keyboard_layout = false hides it.
    {
        auto *layout = find(view.rootObject(), "keyboardLayout");
        auto *label = find(view.rootObject(), "keyboardLayoutText");
        if (!layout || !label || layout->isVisible()) {
            std::cerr << "the keyboard layout indicator shows before the compositor names one\n";
            return 1;
        }
        subscriber->write("keyboard-layout 1 1 us English (US)\n");
        if (!QTest::qWaitFor([&] { return controller.keyboardLayout()["count"].toInt() == 1; }) ||
            layout->isVisible()) {
            std::cerr << "the keyboard layout indicator shows with a single layout\n";
            return 1;
        }
        subscriber->write("keyboard-layout 1 2 us English (US)\n");
        if (!QTest::qWaitFor([&] { return layout->isVisible(); }) ||
            label->property("text").toString() != "us" ||
            controller.keyboardLayout()["name"].toString() != "English (US)" ||
            layout->property("description").toString() != "Keyboard layout: English (US)") {
            std::cerr << "the keyboard layout indicator does not show the active layout\n";
            return 1;
        }
        click(layout);
        if (!QTest::qWaitFor([&] { return label->property("text").toString() == "no"; }) ||
            layoutSwitches != 1 || controller.keyboardLayout()["number"].toInt() != 2) {
            std::cerr << "clicking the keyboard layout did not switch to the next\n";
            return 1;
        }
        if (!controller.widgets()["keyboard_layout"].toBool() ||
            !rewrite(QString(lua).replace("widgets={", "widgets={keyboard_layout=false,")))
            return fail("the keyboard layout widget was off, or the configuration could not be rewritten");
        controller.reload();
        if (!QTest::qWaitFor([&] { return !layout->isVisible(); })) {
            std::cerr << "shell.widgets.keyboard_layout = false did not hide the indicator\n";
            return 1;
        }
        if (!rewrite(lua))
            return fail("could not restore the configuration");
        controller.reload();
        if (!QTest::qWaitFor([&] { return layout->isVisible(); })) {
            std::cerr << "the keyboard layout indicator did not come back\n";
            return 1;
        }
    }
    // The compositor says whether night light is on and who decides.
    if (controller.nightLight() || !controller.nightLightMode().isEmpty())
        return fail("night light is known before the compositor says");
    subscriber->write("night-light on auto\n");
    if (!QTest::qWaitFor([&] { return controller.nightLight() && controller.nightLightMode() == "auto"; }))
        return fail("the night light the compositor reports was not read");
    subscriber->write("night-light off off\n");
    if (!QTest::qWaitFor([&] { return !controller.nightLight() && controller.nightLightMode() == "off"; }))
        return fail("a change of night light was not read");
    // Battery and network widgets show what a (fake) sysfs reports, and only where it exists.
    {
        QDir sys(screens.filePath("sys"));
        auto put = [&](const QString &path, const QString &text) {
            sys.mkpath(QFileInfo(sys.filePath(path)).path());
            QFile f(sys.filePath(path));
            return f.open(QIODevice::WriteOnly | QIODevice::Truncate) && f.write(text.toUtf8()) >= 0;
        };
        SystemStatus fake(sys.path());
        QQmlEngine::setObjectOwnership(&fake, QQmlEngine::CppOwnership);
        view.rootObject()->setProperty("statusSource", QVariant::fromValue(&fake));
        auto *battery = find(view.rootObject(), "batteryWidget");
        auto *network = find(view.rootObject(), "networkWidget");
        if (!battery || !network || battery->isVisible() || network->isVisible()) {
            std::cerr << "battery or network widget shown without hardware\n";
            return 1;
        }
        if (!put("class/power_supply/BAT0/type", "Battery\n") ||
            !put("class/power_supply/BAT0/capacity", "10\n") ||
            !put("class/power_supply/BAT0/status", "Discharging\n") ||
            !put("class/net/wlan0/device", "") || !put("class/net/wlan0/wireless", "") ||
            !put("class/net/wlan0/operstate", "up\n"))
            return fail("could not write the fake sysfs");
        fake.refresh();
        auto *level = find(view.rootObject(), "batteryLevel");
        if (!QTest::qWaitFor([&] { return battery->isVisible() && network->isVisible(); }) ||
            !level || battery->property("low").toBool() != true ||
            network->property("linkDown").toBool()) {
            std::cerr << "battery and network widgets did not appear from sysfs\n";
            return 1;
        }
        const qreal nearlyEmpty = level->width();
        if (!put("class/power_supply/BAT0/capacity", "90\n") ||
            !put("class/power_supply/BAT0/status", "Charging\n") ||
            !put("class/net/wlan0/operstate", "down\n"))
            return fail("could not update the fake sysfs");
        fake.refresh();
        if (!QTest::qWaitFor([&] { return level->width() > nearlyEmpty * 5; }) ||
            battery->property("low").toBool() || !network->property("linkDown").toBool()) {
            std::cerr << "battery and network widgets did not follow sysfs changes\n";
            return 1;
        }
        view.rootObject()->setProperty("statusSource", QVariant::fromValue(controller.status()));
    }
    // The clock wakes once a minute, aimed just past the next minute change, not every second.
    {
        auto *text = find(view.rootObject(), "clock");
        auto *tick = text ? text->findChild<QObject *>("clockTick") : nullptr;
        const int interval = tick ? tick->property("interval").toInt() : 0;
        if (!tick || !tick->property("running").toBool() || interval < 50 || interval > 60050) {
            std::cerr << "the clock does not tick at the minute (interval " << interval << ")\n";
            return 1;
        }
    }
    // The clock opens a month calendar, which pages through months and returns to today.
    {
        auto *clock = find(view.rootObject(), "clockButton");
        auto *calendar = find(view.rootObject(), "calendar");
        if (!clock || !calendar || calendar->isVisible()) {
            std::cerr << "calendar missing or open at start\n";
            return 1;
        }
        click(clock);
        if (!QTest::qWaitFor([&] { return calendar->isVisible(); })) {
            std::cerr << "clicking the clock did not open the calendar\n";
            return 1;
        }
        // Every day of the six-week grid is laid out in its own cell.
        auto *grid = find(view.rootObject(), "monthGrid");
        auto *days = grid ? grid->property("contentItem").value<QQuickItem *>() : nullptr;
        auto laidOut = [&] {
            int cells = 0; // The grid's Repeater is a child too, and has no size.
            for (QQuickItem *day : days ? days->childItems() : QList<QQuickItem *>())
                cells += day->width() >= 20 && day->height() >= 20;
            return cells == 42;
        };
        if (!QTest::qWaitFor(laidOut)) {
            std::cerr << "the calendar's days were not laid out:";
            if (days)
                for (QQuickItem *day : days->childItems())
                    std::cerr << ' ' << day->width() << 'x' << day->height();
            std::cerr << '\n';
            return 1;
        }
        const int month = calendar->property("month").toInt();
        click(find(view.rootObject(), "calendarNext"));
        if (!QTest::qWaitFor([&] { return calendar->property("month").toInt() == (month + 1) % 12; })) {
            std::cerr << "the calendar did not page to the next month\n";
            return 1;
        }
        // A wheel notch down pages to the next month, one up back again.
        auto wheelOn = [&](QQuickItem *item, int delta) {
            QWheelEvent event(centre(item), item->window()->mapToGlobal(centre(item)), {}, {0, delta},
                              Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(item->window(), &event);
        };
        wheelOn(grid, -120);
        if (!QTest::qWaitFor([&] { return calendar->property("month").toInt() == (month + 2) % 12; })) {
            std::cerr << "the wheel did not page the calendar to the next month\n";
            return 1;
        }
        wheelOn(grid, 120);
        if (!QTest::qWaitFor([&] { return calendar->property("month").toInt() == (month + 1) % 12; })) {
            std::cerr << "the wheel did not page the calendar back\n";
            return 1;
        }
        auto *today = find(view.rootObject(), "calendarToday");
        if (!today || !today->isEnabled())
            return fail("the calendar has no Today button, or it does nothing on another month");
        click(today);
        if (!QTest::qWaitFor([&] { return calendar->property("month").toInt() == month; }) ||
            today->isEnabled()) {
            std::cerr << "Today did not return the calendar to this month\n";
            return 1;
        }
        // The title zooms out to the year's months, where one is picked, then to a decade's years.
        auto zoomed = [&] { return calendar->property("view").toString(); };
        auto pick = [&](const char *name, const char *property, int value) -> QQuickItem * {
            std::function<QQuickItem *(QQuickItem *)> search = [&](QQuickItem *item) -> QQuickItem * {
                if (item->objectName() == name && item->property(property).toInt() == value)
                    return item;
                for (auto *child : item->childItems())
                    if (auto *found = search(child))
                        return found;
                return nullptr;
            };
            return search(calendar);
        };
        // What is picked is clicked where it rests, once it has zoomed and slid into place.
        auto settled = [&] {
            return calendar->property("zoom").toReal() == 1 && calendar->property("shift").toReal() == 0;
        };
        auto *title = find(view.rootObject(), "calendarTitle");
        click(title);
        auto *months = find(view.rootObject(), "calendarMonths");
        if (!QTest::qWaitFor([&] { return zoomed() == "months" && months && months->isVisible(); }) ||
            grid->isVisible()) {
            std::cerr << "the calendar's title did not zoom out to the months\n";
            return 1;
        }
        const int later = (month + 3) % 12;
        if (!QTest::qWaitFor(settled))
            return fail("the months did not zoom into place");
        click(pick("calendarMonth", "month", later));
        if (!QTest::qWaitFor([&] { return zoomed() == "days" && calendar->property("month").toInt() == later; })) {
            std::cerr << "picking a month did not show its days\n";
            return 1;
        }
        click(title);
        if (!QTest::qWaitFor([&] { return zoomed() == "months"; }))
            return fail("the calendar's title did not zoom out to the months again");
        click(title);
        auto *years = find(view.rootObject(), "calendarYears");
        const int year = calendar->property("year").toInt();
        if (!QTest::qWaitFor([&] { return zoomed() == "years" && years && years->isVisible(); }) ||
            title->isEnabled()) {
            std::cerr << "the calendar's title did not zoom out to the years\n";
            return 1;
        }
        click(find(view.rootObject(), "calendarNext"));
        if (!QTest::qWaitFor([&] { return calendar->property("year").toInt() == year + 10; })) {
            std::cerr << "the next arrow did not page the years by a decade\n";
            return 1;
        }
        if (!QTest::qWaitFor(settled))
            return fail("the years did not slide into place");
        click(pick("calendarYear", "year", year + 11));
        if (!QTest::qWaitFor([&] { return zoomed() == "months" && calendar->property("year").toInt() == year + 11; })) {
            std::cerr << "picking a year did not show its months\n";
            return 1;
        }
        click(today);
        if (!QTest::qWaitFor([&] {
                return zoomed() == "days" && calendar->property("month").toInt() == month &&
                       calendar->property("year").toInt() == QDate::currentDate().year();
            })) {
            std::cerr << "Today did not return from the months to this month's days\n";
            return 1;
        }
        click(clock);
        if (!QTest::qWaitFor([&] { return !calendar->isVisible(); })) {
            std::cerr << "clicking the clock again did not close the calendar\n";
            return 1;
        }
    }
    // Context menus: a task's, then the bar's. Stand-in tasks replace the Wayland ones.
    auto *tasks = view.rootObject()->findChild<QQuickItem *>("taskList");
    // It notes what the taskbar's menus ask of the windows, as "minimize 7".
    QQmlComponent fakeTasks(view.engine());
    fakeTasks.setData(R"(import QtQml.Models
ListModel {
    property var requests: []
    function note(request) { requests = requests.concat([request]) }
    function activate(id) { note("activate " + id) }
    function minimize(id) { note("minimize " + id) }
    function maximize(id) { note("maximize " + id) }
    function setFullscreen(id, on) { note("fullscreen " + id + " " + on) }
    function close(id) { note("close " + id) }
    function moveToWorkspace(id, number) { note("workspace " + id + " " + number) }
    function moveToOutput(id, output) { note("output " + id + " " + output) }
    function setSticky(id, on) { note("sticky " + id + " " + on) }
    function setFloating(id, on) { note("floating " + id + " " + on) }
    ListElement { taskId: 7; title: 'Fake'; appId: 'fake'; active: false; minimized: false; urgent: false
                  maximized: false; fullscreen: false; output: 'TEST-1'; workspace: 2; sticky: false
                  floating: false; tiling: false }
})",
                      QUrl());
    QObject *fakeModel = fakeTasks.create();
    if (!tasks || !fakeModel)
        return fail("the task models did not load");
    QQmlEngine::setObjectOwnership(fakeModel, QQmlEngine::CppOwnership);
    view.rootObject()->setProperty("taskSource", QVariant::fromValue(fakeModel));
    // What the menus asked of the stand-in windows since the last call, joined by "|".
    auto taskRequests = [&] {
        const auto asked = fakeModel->property("requests").value<QJSValue>().toVariant().toStringList();
        fakeModel->setProperty("requests", QVariant::fromValue(view.engine()->newArray()));
        return asked.join("|");
    };
    // ListModel's methods take JavaScript arguments, so they are reached through the engine.
    auto editTasks = [&](const QString &body) {
        view.engine()
            ->evaluate("(function(model) { " + body + " })")
            .call({view.engine()->newQObject(fakeModel)});
    };
    QQuickItem *task = nullptr;
    auto listedTask = [&](int index) {
        QQuickItem *item = nullptr;
        return QTest::qWaitFor([&] {
            QMetaObject::invokeMethod(tasks, "itemAtIndex", Q_RETURN_ARG(QQuickItem *, item),
                                      Q_ARG(int, index));
            return item != nullptr;
        })
                   ? item
                   : nullptr;
    };
    if (!(task = listedTask(0)))
        return fail("the first task is not listed");
    // A window's title is its own: shown as plain text, never read as markup.
    {
        editTasks("model.append({taskId: 99, title: '<b>bold</b>', appId: 'evil', active: false, "
                  "minimized: false, urgent: false})");
        QQuickItem *hostile = listedTask(1);
        int shown = 0;
        bool markup = false;
        std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
            if (item->property("text").toString() == "<b>bold</b>") {
                ++shown;
                markup = markup || item->property("textFormat").toInt() != Qt::PlainText;
            }
            for (auto *child : item->childItems())
                walk(child);
        };
        if (hostile)
            walk(hostile);
        if (!hostile || shown == 0 || markup) {
            std::cerr << "a window title is not shown as plain text (" << shown << " shown)\n";
            return 1;
        }
        editTasks("model.remove(1)");
    }
    // A window's button fades and grows in as its window opens, its line drawn out, and shrinks
    // away as it closes, taking no more clicks. Slowed down, so that it is seen on its way.
    {
        if (!slowMotion(true))
            return fail("the animations were not slowed down");
        editTasks("model.append({taskId: 98, title: 'Arriving', appId: 'arriving', active: false, "
                  "minimized: false, urgent: false})");
        // Not the button of the window closed just before, which may still be on its way out.
        QPointer<QQuickItem> arriving;
        if (!QTest::qWaitFor([&] {
                arriving = listedTask(1);
                return arriving && arriving->property("taskId").toInt() == 98;
            }) ||
            arriving->opacity() == 1 || arriving->scale() == 1 || arriving->property("reveal").toReal() == 1)
            return fail("a window's button did not fade and grow in");
        if (!QTest::qWaitFor([&] {
                return arriving && arriving->opacity() == 1 && arriving->scale() == 1 &&
                       arriving->property("reveal").toReal() == 1;
            }))
            return fail("a window's button did not come all the way in");
        editTasks("model.remove(1)");
        if (!QTest::qWaitFor([&] { return arriving && arriving->opacity() < 1; }) ||
            arriving->isEnabled())
            return fail("a closing window's button did not fade out, or still takes clicks");
        if (!QTest::qWaitFor([&] { return !arriving || !arriving->isVisible(); }))
            return fail("a closed window's button stayed on the bar");
        // An icon on the bar following a state crossfades to its new shape, as the tiling
        // button's does.
        auto *tilingIcon = find(tiling, "tilingIcon");
        const bool wasTiling = panelTiling();
        click(tiling);
        if (!tilingIcon || !QTest::qWaitFor([&] { return panelTiling() != wasTiling; }) ||
            tilingIcon->property("leaving").toString().isEmpty() ||
            tilingIcon->property("progress").toReal() == 1)
            return fail("the tiling button's icon did not crossfade to its new shape");
        if (!QTest::qWaitFor([&] {
                return tilingIcon->property("leaving").toString().isEmpty() &&
                       tilingIcon->property("progress").toReal() == 1;
            }))
            return fail("the tiling button's icon did not finish its crossfade");
        click(tiling);
        if (!QTest::qWaitFor([&] { return panelTiling() == wasTiling; }))
            return fail("the tiling button did not toggle tiling back");
        if (!slowMotion(false))
            return fail("the animations did not get their speed back");
    }
    auto *menu = find(view.rootObject(), "contextMenu");
    // Repeater delegates are visual children only, so walk the item tree. Rows of the task
    // menus are named contextMenuItem, contextMenuTitle, contextMenuAction and the like.
    std::function<QQuickItem *(QQuickItem *, const QString &)> findMenuItem =
        [&](QQuickItem *parent, const QString &text) -> QQuickItem * {
        for (auto *item : parent->childItems()) {
            if (item->objectName().startsWith("contextMenu") && item->objectName() != "contextMenu" &&
                item->property("text") == text)
                return item;
            if (auto *found = findMenuItem(item, text))
                return found;
        }
        return nullptr;
    };
    auto menuItem = [&](const QString &text) { return findMenuItem(menu, text); };
    // The whole menu must lie inside the popover.
    auto menuShown = [&] { return inPopover(menu); };
    const QPoint entry = centre(task);
    // Held past the long-press time, which once swallowed the right click.
    QTest::mousePress(&view, Qt::RightButton, Qt::NoModifier, entry);
    QTest::qWait(1000);
    QTest::mouseRelease(&view, Qt::RightButton, Qt::NoModifier, centre(task));
    if (!QTest::qWaitFor([&] {
            return view.rootObject()->property("taskMenuId").toInt() == 7 && menuShown();
        }) ||
        !menuItem("Maximize") || !menuItem("Minimize") || !menuItem("Close window")) {
        std::cerr << "right-clicking a task did not show its menu\n";
        return 1;
    }
    click(menuItem("Minimize"));
    if (!QTest::qWaitFor([&] { return !view.rootObject()->property("menuOpen").toBool(); }) ||
        !QTest::qWaitFor([&] { return !popover->isVisible(); })) {
        std::cerr << "choosing a task menu item did not close the menu\n";
        return 1;
    }
    if (const auto asked = taskRequests(); asked != "minimize 7") {
        std::cerr << "minimizing from a task's menu asked " << asked.toStdString() << '\n';
        return 1;
    }
    // The window's entries follow its state, while the menu is open too: maximized, it offers to
    // restore it, and fullscreen is checked; minimized, it offers only to restore it.
    {
        auto checked = [&](const QString &text) {
            return menuItem(text) && menuItem(text)->property("marked").toBool();
        };
        click(task, Qt::RightButton);
        if (!QTest::qWaitFor([&] { return menuShown() && menuItem("Maximize"); }) ||
            !menuItem("Fullscreen") || checked("Fullscreen") || menuItem("Restore"))
            return fail("a window's menu does not offer to maximize it and make it fullscreen");
        editTasks("model.setProperty(0, 'maximized', true); model.setProperty(0, 'fullscreen', true)");
        if (!QTest::qWaitFor([&] {
                return menuItem("Restore") && !menuItem("Maximize") && checked("Fullscreen") &&
                       menuItem("Restore")->property("modelData").toMap()["icon"] == "copy";
            }))
            return fail("the window's menu did not follow it maximized and fullscreen");
        click(menuItem("Fullscreen"));
        if (const auto asked = taskRequests(); asked != "fullscreen 7 false") {
            std::cerr << "leaving fullscreen from a task's menu asked " << asked.toStdString() << '\n';
            return 1;
        }
        editTasks("model.setProperty(0, 'minimized', true)");
        click(task, Qt::RightButton);
        if (!QTest::qWaitFor([&] { return menuShown() && menuItem("Restore"); }) ||
            menuItem("Minimize") || menuItem("Maximize") || menuItem("Fullscreen"))
            return fail("a minimized window's menu offers more than to restore it");
        click(menuItem("Restore"));
        if (const auto asked = taskRequests(); asked != "activate 7") {
            std::cerr << "restoring from a task's menu asked " << asked.toStdString() << '\n';
            return 1;
        }
        editTasks("model.setProperty(0, 'minimized', false); model.setProperty(0, 'maximized', false); "
                  "model.setProperty(0, 'fullscreen', false)");
        if (!QTest::qWaitFor([&] { return !popover->isVisible(); }))
            return fail("the popover did not close after restoring");
    }
    // Moving the window: its monitor's workspaces, by name where they have one, the one it is on
    // marked; a sticky window is on none of them.
    {
        click(task, Qt::RightButton);
        if (!QTest::qWaitFor([&] { return menuShown() && menuItem("Move to workspace"); }))
            return fail("a window's menu does not offer to move it to another workspace");
        click(menuItem("Move to workspace"));
        auto marked = [&](const QString &text) {
            return menuItem(text) && menuItem(text)->property("marked").toBool();
        };
        if (!QTest::qWaitFor([&] {
                return menuItem("web") && menuItem("Workspace 2") && menuItem("Workspace 3") &&
                       menuItem("mail") && marked("Workspace 2") && !marked("web");
            }))
            return fail("the workspace submenu does not list the workspaces, the window's marked");
        editTasks("model.setProperty(0, 'sticky', true)");
        if (!QTest::qWaitFor([&] { return menuItem("mail") && !marked("Workspace 2"); }))
            return fail("the workspace submenu marks a workspace for a sticky window");
        click(menuItem("mail"));
        if (const auto asked = taskRequests(); asked != "workspace 7 4") {
            std::cerr << "moving to a workspace from a task's menu asked " << asked.toStdString() << '\n';
            return 1;
        }
        editTasks("model.setProperty(0, 'sticky', false)");
        if (!QTest::qWaitFor([&] { return !popover->isVisible(); }))
            return fail("the popover did not close after moving the window");
        // With one monitor there is no other to move it to; with two, its own is marked.
        click(task, Qt::RightButton);
        if (!QTest::qWaitFor([&] { return menuShown() && menuItem("Move to workspace"); }) ||
            menuItem("Move to monitor"))
            return fail("a window's menu offers other monitors with only one");
        subscriber->write(state(toggled, currentWorkspace) + "output OTHER-1 1 - off\n");
        if (!QTest::qWaitFor([&] { return menuItem("Move to monitor"); }))
            return fail("a window's menu does not offer to move it to another monitor");
        click(menuItem("Move to monitor"));
        if (!QTest::qWaitFor([&] { return menuItem("OTHER-1") && marked(output); }) || marked("OTHER-1"))
            return fail("the monitor submenu does not list the monitors, the window's marked");
        click(menuItem("OTHER-1"));
        if (const auto asked = taskRequests(); asked != "output 7 OTHER-1") {
            std::cerr << "moving to a monitor from a task's menu asked " << asked.toStdString() << '\n';
            return 1;
        }
        subscriber->write(state(toggled, currentWorkspace));
        if (!QTest::qWaitFor([&] { return !popover->isVisible() && controller.workspaces().size() == 1; }))
            return fail("the popover did not close after moving the window to another monitor");
    }
    // Keeping it on every workspace, and floating it where its workspace tiles.
    {
        click(task, Qt::RightButton);
        if (!QTest::qWaitFor([&] { return menuShown() && menuItem("Keep on all workspaces"); }) ||
            menuItem("Keep on all workspaces")->property("marked").toBool() || menuItem("Float"))
            return fail("a window's menu does not offer to keep it on all workspaces, or floats it "
                        "where nothing tiles");
        editTasks("model.setProperty(0, 'tiling', true)");
        if (!QTest::qWaitFor([&] { return menuItem("Float"); }) ||
            menuItem("Float")->property("marked").toBool())
            return fail("a tiled window's menu does not offer to float it");
        click(menuItem("Float"));
        click(task, Qt::RightButton);
        if (!QTest::qWaitFor([&] { return menuShown() && menuItem("Keep on all workspaces"); }))
            return fail("the window's menu did not open again");
        click(menuItem("Keep on all workspaces"));
        if (const auto asked = taskRequests(); asked != "floating 7 true|sticky 7 true") {
            std::cerr << "floating and sticking from a task's menu asked " << asked.toStdString() << '\n';
            return 1;
        }
        editTasks("model.setProperty(0, 'tiling', false)");
        if (!QTest::qWaitFor([&] { return !popover->isVisible(); }))
            return fail("the popover did not close after making the window sticky");
    }
    // Closing the window comes last, in the danger colour.
    {
        click(task, Qt::RightButton);
        if (!QTest::qWaitFor([&] { return menuShown() && menuItem("Close window"); }))
            return fail("the window's menu did not open to close it");
        const auto entries = menu->property("entries").value<QJSValue>();
        const auto last = entries.property(entries.property("length").toUInt() - 1);
        if (last.property("text").toString() != "Close window" ||
            !menuItem("Close window")->property("danger").toBool() ||
            menuItem("Close window")->objectName() != "contextMenuClose")
            return fail("closing a window is not the last entry of its menu, in the danger colour");
        click(menuItem("Close window"));
        if (const auto asked = taskRequests(); asked != "close 7") {
            std::cerr << "closing from a task's menu asked " << asked.toStdString() << '\n';
            return 1;
        }
        if (!QTest::qWaitFor([&] { return !popover->isVisible(); }))
            return fail("the popover did not close after closing the window");
    }
    // A stacked button's menu is about all its windows: counted under the title, minimized or
    // restored, moved and closed together, the workspace they share marked.
    {
        editTasks("model.append({ taskId: 9, title: 'Second', appId: 'fake', active: false, "
                  "minimized: false, urgent: false, maximized: false, fullscreen: false, "
                  "output: 'TEST-1', workspace: 2, sticky: false, floating: false, tiling: false })");
        if (!QTest::qWaitFor([&] { return task->property("stacked").toBool(); }))
            return fail("the application's two windows did not stack");
        auto title = [&] { return find(view.rootObject(), "contextMenuTitle"); };
        click(task, Qt::RightButton);
        if (!QTest::qWaitFor([&] {
                return menuShown() && title() && menuItem("Close all 2 windows") && menuItem("Minimize all");
            }) ||
            title()->property("modelData").toMap()["secondary"] != "2 windows" || menuItem("Maximize") ||
            menuItem("Fullscreen") || menuItem("Close window"))
            return fail("a stacked button's menu is not about all its windows");
        click(menuItem("Move to workspace"));
        if (!QTest::qWaitFor([&] {
                return menuItem("Workspace 2") && menuItem("Workspace 2")->property("marked").toBool();
            }))
            return fail("the workspace both windows are on is not marked");
        click(menuItem("web"));
        if (const auto asked = taskRequests(); asked != "workspace 7 1|workspace 9 1") {
            std::cerr << "moving a stack to a workspace asked " << asked.toStdString() << '\n';
            return 1;
        }
        click(task, Qt::RightButton);
        if (!QTest::qWaitFor([&] { return menuShown() && menuItem("Close all 2 windows"); }))
            return fail("the stacked button's menu did not open again");
        click(menuItem("Close all 2 windows"));
        if (const auto asked = taskRequests(); asked != "close 7|close 9") {
            std::cerr << "closing a stack asked " << asked.toStdString() << '\n';
            return 1;
        }
        editTasks("model.setProperty(0, 'minimized', true); model.setProperty(1, 'minimized', true)");
        click(task, Qt::RightButton);
        if (!QTest::qWaitFor([&] { return menuShown() && menuItem("Restore all"); }) ||
            menuItem("Minimize all"))
            return fail("a stack of minimized windows does not offer to restore them");
        click(menuItem("Restore all"));
        if (const auto asked = taskRequests(); asked != "activate 7|activate 9") {
            std::cerr << "restoring a stack asked " << asked.toStdString() << '\n';
            return 1;
        }
        editTasks("model.remove(1); model.setProperty(0, 'minimized', false)");
        QMetaObject::invokeMethod(tasks, "forceLayout");
        if (!QTest::qWaitFor([&] { return !popover->isVisible() && !task->property("stacked").toBool(); }))
            return fail("the popover did not close after restoring the stack");
    }
    // The task's window belongs to an installed application, which its menu pins. Pinned, the
    // window takes over the application's slot instead of adding a button; with no window left
    // the slot's launcher returns, and its own menu unpins it. Pins are remembered in the state
    // directory.
    auto readPins = [&] {
        QFile file(pins);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    };
    auto pinned = [&] { return find(view.rootObject(), "pinned:shaodesk-test-app.desktop"); };
    auto pinnedTask = [&] { return find(view.rootObject(), "pinnedTask:shaodesk-test-app.desktop"); };
    click(task, Qt::RightButton);
    if (!QTest::qWaitFor([&] { return menuShown() && menuItem("Pin to taskbar"); })) {
        std::cerr << "a task's menu did not offer to pin its application\n";
        return 1;
    }
    click(menuItem("Pin to taskbar"));
    if (!QTest::qWaitFor([&] { return pinned() != nullptr; }) ||
        !controller.isPinned("shaodesk-test-app.desktop") ||
        readPins() != "shaodesk-test-app.desktop\n") {
        std::cerr << "pinning a task's application did not add and save a taskbar button\n";
        return 1;
    }
    if (!QTest::qWaitFor([&] {
            return pinnedTask() && pinnedTask()->isVisible() && !pinned()->isVisible() &&
                   tasks->property("count").toInt() == 0;
        })) {
        std::cerr << "a pinned application's window did not take over its slot\n";
        return 1;
    }
    if (!QTest::qWaitFor([&] { return !popover->isVisible(); })) {
        std::cerr << "the popover did not close after pinning\n";
        return 1;
    }
    // Dragging a pinned slot's window onto another pinned slot moves the pin there.
    controller.pin("shaodesk-test-other.desktop");
    auto other = [&] { return find(view.rootObject(), "pinned:shaodesk-test-other.desktop"); };
    if (!QTest::qWaitFor([&] {
            return other() && other()->isVisible() &&
                   centre(other()).x() > centre(pinnedTask()).x();
        }))
        return fail("the dragged pin did not move past the other task");
    {
        const QPoint from = centre(pinnedTask()), to = centre(other());
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, from);
        for (int step = 1; step <= 10; ++step) {
            QTest::mouseMove(&view, from + (to - from) * step / 10);
            QTest::qWait(10);
        }
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, to);
    }
    if (!QTest::qWaitFor(
            [&] { return readPins() == "shaodesk-test-other.desktop\nshaodesk-test-app.desktop\n"; }) ||
        !QTest::qWaitFor([&] {
            return pinnedTask() && other() &&
                   other()->mapToScene({0, 0}).x() < pinnedTask()->mapToScene({0, 0}).x();
        })) {
        std::cerr << "dragging a pinned window onto another pinned slot did not move its pin\n";
        return 1;
    }
    controller.unpin("shaodesk-test-other.desktop");
    editTasks("model.remove(0)");
    if (!QTest::qWaitFor([&] { return !pinnedTask() && pinned()->isVisible(); })) {
        std::cerr << "a pinned slot did not show its launcher again once its window closed\n";
        return 1;
    }
    // Slowed down: a window opening in a pinned slot draws its line out under the launcher's
    // icon, which stays, and an application just pinned fades and grows into a slot opening
    // for it.
    {
        if (!slowMotion(true))
            return fail("the animations were not slowed down");
        editTasks("model.append({taskId: 12, title: 'Fake again', appId: 'fake', active: false, "
                  "minimized: false, urgent: false})");
        QPointer<QQuickItem> arrived;
        if (!QTest::qWaitFor([&] { return (arrived = pinnedTask()) != nullptr; }) ||
            arrived->property("reveal").toReal() == 1 || arrived->opacity() != 1 || arrived->scale() != 1)
            return fail("a window opening in a pinned slot did not only draw its line out");
        if (!QTest::qWaitFor([&] { return arrived && arrived->property("reveal").toReal() == 1; }))
            return fail("the line of a window opening in a pinned slot was not drawn all the way");
        editTasks("model.remove(0)");
        controller.pin("shaodesk-test-other.desktop");
        QPointer<QQuickItem> slot;
        if (!QTest::qWaitFor([&] { return other() && (slot = other()->parentItem()); }) ||
            slot->property("grow").toReal() == 1 || slot->opacity() == 1)
            return fail("an application just pinned did not grow into its slot");
        if (!QTest::qWaitFor([&] { return slot && slot->property("grow").toReal() == 1; }))
            return fail("an application just pinned did not come all the way in");
        if (pinned()->parentItem()->property("grow").toReal() != 1)
            return fail("a slot that was there already came in again with another");
        controller.unpin("shaodesk-test-other.desktop");
        if (!slowMotion(false))
            return fail("the animations did not get their speed back");
        if (!QTest::qWaitFor([&] { return !other() && !pinnedTask() && pinned()->isVisible(); }))
            return fail("the pinned slots did not go back as they were");
    }
    click(pinned(), Qt::RightButton);
    if (!QTest::qWaitFor([&] { return menuShown() && menuItem("Unpin from taskbar"); }) ||
        !menuItem("Open") || !menuItem("Fake app") ||
        menuItem("Fake app")->objectName() != "contextMenuTitle") {
        std::cerr << "a pinned application's menu did not offer to unpin it\n";
        return 1;
    }
    // One with desktop actions offers them before opening it, and nothing of a window's.
    {
        QTest::keyClick(popover, Qt::Key_Escape);
        controller.pin("shaodesk-test-actions.desktop");
        auto withActions = [&] { return find(view.rootObject(), "pinned:shaodesk-test-actions.desktop"); };
        if (!QTest::qWaitFor([&] { return withActions() && withActions()->isVisible() && !popover->isVisible(); }))
            return fail("the application with desktop actions was not pinned");
        click(withActions(), Qt::RightButton);
        if (!QTest::qWaitFor([&] { return menuShown() && menuItem("Touch a file"); }) ||
            !menuItem("Missing program") || !menuItem("Open") || !menuItem("Unpin from taskbar") ||
            menuItem("New window") || menuItem("Minimize") || menuItem("Close window"))
            return fail("a pinned application's menu does not offer its desktop actions and to open it");
        const auto entries = menu->property("entries").value<QJSValue>();
        QStringList order; // the title or text of each entry, "-" for a separator
        for (quint32 i = 0; i < entries.property("length").toUInt(); ++i) {
            const auto entry = entries.property(i);
            order << (entry.property("separator").toBool() ? QString("-")
                      : entry.property("title").isString() ? entry.property("title").toString()
                                                           : entry.property("text").toString());
        }
        if (order.join("|") != "Action app|-|Touch a file|Missing program|Open|-|Unpin from taskbar") {
            std::cerr << "a pinned application's menu is in the wrong order: "
                      << order.join("|").toStdString() << '\n';
            return 1;
        }
        QTest::keyClick(popover, Qt::Key_Escape);
        controller.unpin("shaodesk-test-actions.desktop");
        if (!QTest::qWaitFor([&] { return !withActions() && !popover->isVisible(); }))
            return fail("the application with desktop actions was not unpinned");
        click(pinned(), Qt::RightButton);
        if (!QTest::qWaitFor([&] { return menuShown() && menuItem("Unpin from taskbar"); }))
            return fail("the pinned application's menu did not open again");
    }
    click(menuItem("Unpin from taskbar"));
    if (!QTest::qWaitFor([&] { return pinned() == nullptr; }) || !readPins().isEmpty() ||
        controller.isPinned("shaodesk-test-app.desktop")) {
        std::cerr << "unpinning did not remove and forget the taskbar button\n";
        return 1;
    }
    if (!QTest::qWaitFor([&] { return !popover->isVisible(); })) {
        std::cerr << "the popover did not close after unpinning\n";
        return 1;
    }
    editTasks("model.append({ taskId: 7, title: 'Fake', appId: 'fake', active: false, "
              "minimized: false, urgent: false })");
    if (!(task = listedTask(0)))
        return fail("the re-added task is not listed");
    // A window's menu is headed by its application's name and the window's title. One of an
    // application with desktop actions offers them, with their icons, and to start it again.
    {
        auto title = [&] { return find(view.rootObject(), "contextMenuTitle"); };
        click(task, Qt::RightButton);
        if (!QTest::qWaitFor([&] { return menuShown() && title(); }) ||
            title()->property("text") != "Fake app" ||
            title()->property("modelData").toMap()["secondary"] != "Fake" || !menuItem("New window")) {
            std::cerr << "a window's menu is not headed by its application and title\n";
            return 1;
        }
        // The keyboard passes over the title.
        QTest::keyClick(popover, Qt::Key_Home);
        if (!QTest::qWaitFor([&] { return menuItem("New window")->property("highlighted").toBool(); }) ||
            title()->property("highlighted").toBool())
            return fail("Home did not go to the first entry under the title");
        QTest::keyClick(popover, Qt::Key_Escape);
        editTasks("model.append({ taskId: 8, title: 'Report', appId: 'shaodesk-test-actions', "
                  "active: false, minimized: false, urgent: false })");
        QQuickItem *withActions = listedTask(1);
        if (!withActions)
            return fail("the window with desktop actions is not listed");
        click(withActions, Qt::RightButton);
        if (!QTest::qWaitFor([&] {
                return menuShown() && title() && title()->property("text") == "Action app" &&
                       menuItem("Touch a file") && menuItem("Missing program") &&
                       menuItem("New window");
            }) ||
            menuItem("Touch a file")->objectName() != "contextMenuAction" ||
            menuItem("Touch a file")->property("modelData").toMap()["icon"] != "document-new") {
            std::cerr << "a window's menu does not offer its application's desktop actions\n";
            return 1;
        }
        QFile::remove(actionMarker);
        click(menuItem("Touch a file"));
        if (!QTest::qWaitFor([&] { return QFile::exists(actionMarker); }) ||
            !QTest::qWaitFor([&] { return !view.rootObject()->property("menuOpen").toBool(); })) {
            std::cerr << "a desktop action in a window's menu did not run\n";
            return 1;
        }
        editTasks("model.remove(1)");
        // The list drops the button when it next lays itself out.
        QMetaObject::invokeMethod(tasks, "forceLayout");
    }
    // Empty bar space, right of the only task, opens the bar menu.
    const QPoint empty =
        task->mapToScene(QPointF(task->width() + 40, task->height() / 2)).toPoint();
    QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, empty);
    if (!QTest::qWaitFor(
            [&] { return view.rootObject()->property("barMenuOpen").toBool() && menuShown(); }) ||
        !menuItem("Turn tiling off") || !menuItem("Applications")) {
        std::cerr << "right-clicking empty bar space did not show the bar menu\n";
        return 1;
    }
    // While a menu is open the popover holds the keyboard and takes the pointer everywhere but
    // over the bar, whose buttons stay reachable: another one opens its popup in one press. A
    // press beside the popups closes them, and the popover goes, taking nothing any more.
    {
        const QRegion outsideBar(0, 0, popover->width(), popover->height() - view.height());
        if (!popover->keyboard() || popover->inputRegion() != outsideBar) {
            std::cerr << "an open menu does not take the keyboard and the pointer beside the bar\n";
            return 1;
        }
        auto *calendar = find(view.rootObject(), "calendar");
        click(find(view.rootObject(), "clockButton"));
        if (!QTest::qWaitFor([&] { return calendar && inPopover(calendar); }) ||
            view.rootObject()->property("barMenuOpen").toBool() || !popover->isVisible()) {
            std::cerr << "a press on the clock while the bar menu was open did not switch to the calendar\n";
            return 1;
        }
        QTest::mouseClick(popover, Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
        if (!QTest::qWaitFor([&] { return !calendar->isVisible() && !popover->isVisible(); }) ||
            view.rootObject()->property("menuOpen").toBool() || popover->keyboard() ||
            !popover->inputRegion().isEmpty()) {
            std::cerr << "a press beside the calendar did not close it and the popover\n";
            return 1;
        }
        QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, empty);
        if (!QTest::qWaitFor([&] { return view.rootObject()->property("barMenuOpen").toBool() && menuShown(); }))
            return fail("the bar menu did not open again");
    }
    click(menuItem("Turn tiling off"));
    if (!QTest::qWaitFor([&] { return !toggled && !panelTiling(); }) ||
        view.rootObject()->property("menuOpen").toBool()) {
        std::cerr << "the bar menu did not toggle tiling off\n";
        return 1;
    }
    // The bar menu's appearance entry opens the profiles beside it, the one in use marked, and
    // the menu stays; picking one switches.
    QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, empty);
    if (!QTest::qWaitFor([&] {
            return menuShown() && menuItem("Appearance") &&
                   menuItem("Appearance")->property("modelData").toMap()["secondary"] == "dark";
        })) {
        std::cerr << "the bar menu lacks the appearance profiles\n";
        return 1;
    }
    // The card a row is on, and whether one card lies right of another.
    auto cardOf = [](QQuickItem *row) {
        while (row && !row->property("anchorRect").isValid())
            row = row->parentItem();
        return row;
    };
    auto beside = [](QQuickItem *left, QQuickItem *right) {
        const QRectF a = left->mapRectToScene(QRectF(0, 0, left->width(), left->height()));
        const QRectF b = right->mapRectToScene(QRectF(0, 0, right->width(), right->height()));
        return b.left() >= a.right() - 1 && b.top() < a.bottom() && b.bottom() > a.top();
    };
    click(menuItem("Appearance"));
    if (!QTest::qWaitFor([&] {
            return menuShown() && menuItem("dark") && menuItem("light") && menuItem("Applications") &&
                   menuItem("dark")->property("marked").toBool() &&
                   !menuItem("light")->property("marked").toBool() &&
                   menuItem("Appearance")->property("expanded").toBool() &&
                   cardOf(menuItem("dark")) != cardOf(menuItem("Applications")) &&
                   beside(cardOf(menuItem("Applications")), cardOf(menuItem("dark"))) &&
                   inPopover(cardOf(menuItem("dark")));
        })) {
        std::cerr << "the appearance entry did not open the profiles beside the menu\n";
        return 1;
    }
    click(menuItem("light"));
    if (!QTest::qWaitFor([&] {
            return requests == QStringList{"profile light"} && controller.profile() == "light" &&
                   controller.accent() == QColor("#336699") &&
                   !view.rootObject()->property("barMenuOpen").toBool();
        })) {
        std::cerr << "picking a profile did not switch to it: " << requests.join("|").toStdString()
                  << " " << controller.profile().toStdString() << '\n';
        return 1;
    }
    requests.clear();
    QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, empty);
    if (!QTest::qWaitFor([&] {
            return menuShown() && menuItem("Appearance") && !menuItem("dark") &&
                   menuItem("Appearance")->property("modelData").toMap()["secondary"] == "light";
        })) {
        std::cerr << "the bar menu did not open without its submenu, with the new profile\n";
        return 1;
    }
    // The keyboard: nothing is highlighted after a right click; Down and Up move, wrapping and
    // skipping nothing that can be chosen; End and Home go to the ends; Right opens a submenu at
    // its first entry, Left closes it; Escape closes a submenu, then the menu. The pointer rests
    // away from the menu, where it would select what opens under it.
    QTest::mouseMove(popover, QPoint(5, 5));
    {
        auto highlighted = [&] {
            QStringList rows;
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                for (auto *child : item->childItems()) {
                    if (child->objectName() == "contextMenuItem" && child->isVisible() &&
                        child->property("highlighted").toBool())
                        rows << child->property("text").toString();
                    walk(child);
                }
            };
            walk(menu);
            return rows.join("|");
        };
        auto key = [&](Qt::Key key, const QString &expected) {
            QTest::keyClick(popover, key);
            return QTest::qWaitFor([&] { return highlighted() == expected; });
        };
        if (!highlighted().isEmpty() || !key(Qt::Key_Down, "Turn tiling on") ||
            !key(Qt::Key_Up, "Appearance") || !key(Qt::Key_Up, "Show desktop") ||
            !key(Qt::Key_Home, "Turn tiling on") || !key(Qt::Key_End, "Appearance") ||
            !key(Qt::Key_Down, "Turn tiling on")) {
            std::cerr << "the arrows, Home and End did not move through the bar menu: "
                      << highlighted().toStdString() << '\n';
            return 1;
        }
        if (!key(Qt::Key_End, "Appearance") || !key(Qt::Key_Right, "Appearance|dark") ||
            !key(Qt::Key_Down, "Appearance|light") || !key(Qt::Key_Down, "Appearance|dark") ||
            !key(Qt::Key_Left, "Appearance") || menuItem("dark")) {
            std::cerr << "Right and Left did not open and close the submenu: "
                      << highlighted().toStdString() << '\n';
            return 1;
        }
        if (!key(Qt::Key_Return, "Appearance|dark") || !key(Qt::Key_Escape, "Appearance") ||
            !view.rootObject()->property("barMenuOpen").toBool()) {
            std::cerr << "Enter did not open the submenu, or Escape closed more than it: "
                      << highlighted().toStdString() << " "
                      << view.rootObject()->property("barMenuOpen").toBool() << '\n';
            return 1;
        }
        QTest::keyClick(popover, Qt::Key_Escape);
        if (!QTest::qWaitFor([&] { return !view.rootObject()->property("menuOpen").toBool(); })) {
            std::cerr << "Escape did not close the bar menu\n";
            return 1;
        }
    }
    // The pointer resting on a submenu's entry opens it, and resting on another closes it; the
    // pointer crossing other entries on its way into the submenu leaves it open.
    QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, empty);
    if (!QTest::qWaitFor([&] { return menuShown() && menuItem("Appearance") && !menuItem("dark"); }))
        return fail("the bar menu did not open again");
    QTest::mouseMove(popover, centre(menuItem("Appearance")));
    if (!QTest::qWaitFor([&] { return menuItem("dark") && menuItem("dark")->isVisible(); })) {
        std::cerr << "resting on the appearance entry did not open its submenu\n";
        return 1;
    }
    QTest::mouseMove(popover, centre(menuItem("Show desktop")));
    QTest::mouseMove(popover, centre(menuItem("light")));
    QTest::qWait(400);
    if (!menuItem("light") || !menuItem("Appearance")->property("expanded").toBool()) {
        std::cerr << "crossing another entry into the submenu closed it\n";
        return 1;
    }
    QTest::mouseMove(popover, centre(menuItem("Show desktop")));
    if (!QTest::qWaitFor([&] { return !menuItem("light"); })) {
        std::cerr << "resting on another entry did not close the submenu\n";
        return 1;
    }
    click(menuItem("Show desktop"));
    // The profile button on the bar lists the profiles, the one in use marked, and switches.
    auto *profilesButton = find(view.rootObject(), "profilesButton");
    if (!QTest::qWaitFor([&] { return !view.rootObject()->property("menuOpen").toBool(); }) ||
        !profilesButton || !profilesButton->isVisible()) {
        std::cerr << "the bar lacks the profile button\n";
        return 1;
    }
    click(profilesButton);
    std::function<QQuickItem *(QQuickItem *, const QString &)> findProfile =
        [&](QQuickItem *parent, const QString &name) -> QQuickItem * {
        for (auto *item : parent->childItems()) {
            if (item->objectName() == "profileItem" && item->property("text") == name)
                return item;
            if (auto *found = findProfile(item, name))
                return found;
        }
        return nullptr;
    };
    QQuickItem *profileList = nullptr;
    if (!QTest::qWaitFor([&] {
            profileList = find(view.rootObject(), "profileList");
            return profileList && profileList->isVisible() && findProfile(profileList, "dark") &&
                   findProfile(profileList, "light") &&
                   findProfile(profileList, "light")->property("marked").toBool() &&
                   !findProfile(profileList, "dark")->property("marked").toBool() &&
                   inPopover(profileList);
        })) {
        std::cerr << "the profile button did not list the profiles\n";
        return 1;
    }
    click(findProfile(profileList, "dark"));
    if (!QTest::qWaitFor([&] {
            return requests == QStringList{"profile dark"} && controller.profile() == "dark" &&
                   !profileList->isVisible();
        })) {
        std::cerr << "picking a profile from the bar did not switch to it\n";
        return 1;
    }
    requests.clear();
    // The wallpaper button shows the pictures of shell.wallpapers as thumbnails, the one in use
    // marked; clicking another shows it, keeps it for the profile and leaves the picker open.
    auto *wallpapersButton = find(view.rootObject(), "wallpapersButton");
    if (!wallpapersButton || !wallpapersButton->isVisible()) {
        std::cerr << "the bar lacks the wallpaper button\n";
        return 1;
    }
    click(wallpapersButton);
    std::function<void(QQuickItem *, QList<QQuickItem *> &)> wallpaperItems =
        [&](QQuickItem *parent, QList<QQuickItem *> &found) {
            for (auto *item : parent->childItems()) {
                if (item->objectName() == "wallpaperItem" && item->isVisible())
                    found << item;
                wallpaperItems(item, found);
            }
        };
    auto wallpaperItem = [&](const QString &name) -> QQuickItem * {
        QList<QQuickItem *> found;
        if (auto *picker = find(view.rootObject(), "wallpaperPicker"))
            wallpaperItems(picker, found);
        for (auto *item : found)
            if (item->property("modelData").toMap().value("name") == name)
                return item;
        return nullptr;
    };
    QQuickItem *picker = nullptr;
    if (!QTest::qWaitFor([&] {
            picker = find(view.rootObject(), "wallpaperPicker");
            return picker && picker->isVisible() && wallpaperItem("one") && wallpaperItem("two") &&
                   wallpaperItem("one")->property("current").toBool() &&
                   !wallpaperItem("two")->property("current").toBool() && inPopover(picker);
        })) {
        std::cerr << "the wallpaper button did not show the pictures: " << (picker ? picker->isVisible() : -1)
                  << " " << controller.wallpapers().size() << " " << !!wallpaperItem("one") << !!wallpaperItem("two")
                  << " " << (picker ? picker->height() : 0) << " " << popover->height() << '\n';
        return 1;
    }
    click(wallpaperItem("two"));
    const auto picked = screens.filePath("state/shaodesk/wallpapers");
    auto pickedText = [&picked] {
        QFile f(picked);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
    };
    if (!QTest::qWaitFor([&] {
            return controller.wallpaperFile() == walls + "/b/two.png" &&
                   controller.wallpaper() == QUrl::fromLocalFile(walls + "/b/two.png") &&
                   wallpaperItem("two")->property("current").toBool() && picker->isVisible() &&
                   pickedText() == "dark\twalls/a/one.png\t" + walls + "/b/two.png\n";
        })) {
        std::cerr << "picking a wallpaper did not show and keep it: " << pickedText().toStdString() << '\n';
        return 1;
    }
    if (!QTest::qWaitFor([&] {
            return QDir(screens.filePath("cache/thumbnails/large")).entryList(QDir::Files).size() == 2;
        })) {
        std::cerr << "the wallpaper thumbnails were not cached\n";
        return 1;
    }
    controller.pickWallpaper("");
    if (controller.wallpaperFile() != directory.filePath("walls/a/one.png") || pickedText() != "") {
        std::cerr << "clearing the picked wallpaper did not go back to the configured one\n";
        return 1;
    }
    click(wallpapersButton);
    if (!QTest::qWaitFor([&] { return !picker->isVisible(); })) {
        std::cerr << "the wallpaper button did not close the picker\n";
        return 1;
    }
    // A task asking for attention is marked, and unmarked when it stops.
    if (task->property("shownUrgent").toBool() || find(task, "taskUrgent")->isVisible()) {
        std::cerr << "a task is marked urgent before it asked\n";
        return 1;
    }
    editTasks("model.setProperty(0, 'urgent', true)");
    if (!QTest::qWaitFor([&] {
            return task->property("shownUrgent").toBool() && find(task, "taskUrgent")->isVisible();
        })) {
        std::cerr << "a task asking for attention is not marked\n";
        return 1;
    }
    editTasks("model.setProperty(0, 'urgent', false)");
    if (!QTest::qWaitFor([&] {
            return !task->property("shownUrgent").toBool() && !find(task, "taskUrgent")->isVisible();
        })) {
        std::cerr << "a task that stopped asking for attention is still marked\n";
        return 1;
    }
    // Dragging a task along the bar moves it, not the whole list, as far as it is dragged.
    // Each belongs to a different application, or they would share a button.
    editTasks("model.append({ taskId: 8, title: 'Second', appId: 'second', active: false, "
              "minimized: false, urgent: false })");
    editTasks("model.append({ taskId: 9, title: 'Third', appId: 'third', active: false, "
              "minimized: false, urgent: false })");
    QQuickItem *third = listedTask(2);
    if (!third)
        return fail("the third task is not listed");
    auto taskIdAt = [&](int index) {
        QJSValue row;
        QMetaObject::invokeMethod(fakeModel, "get", Q_RETURN_ARG(QJSValue, row),
                                  Q_ARG(int, index));
        return row.property("taskId").toInt();
    };
    const QPoint from = centre(task), to = centre(third) + QPoint(third->width() / 4, 0);
    QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, from);
    for (int step = 1; step <= 10; ++step) {
        QTest::mouseMove(&view, from + (to - from) * step / 10);
        QTest::qWait(10);
    }
    QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, to);
    if (!QTest::qWaitFor([&] { return taskIdAt(2) == 7; }) || taskIdAt(0) != 8 ||
        tasks->property("contentX").toReal() != 0) {
        std::cerr << "dragging a task did not reorder the task list\n";
        return 1;
    }
    // An application's windows share one stacked button showing how many there are.
    editTasks("model.append({ taskId: 10, title: 'Group one', appId: 'grouped', active: false, "
              "minimized: false, urgent: false })");
    editTasks("model.append({ taskId: 11, title: 'Group two', appId: 'grouped', active: true, "
              "minimized: false, urgent: false })");
    QQuickItem *stack = nullptr;
    if (!QTest::qWaitFor([&] {
            stack = listedTask(3);
            auto *stackCount = stack ? find(stack, "taskCount") : nullptr;
            return tasks->property("count").toInt() == 4 && stackCount &&
                   stack->property("stacked").toBool() && stack->property("shownActive").toBool() &&
                   stackCount->isVisible();
        })) {
        std::cerr << "an application's windows did not share one stacked task button\n";
        return 1;
    }
    // Hovering it lists both windows above the bar, without taking the keyboard; leaving hides
    // the list and shrinks the panel again.
    auto *groupList = find(view.rootObject(), "groupList");
    auto groupRows = [&] {
        int rows = 0;
        std::function<void(QQuickItem *)> count = [&](QQuickItem *item) {
            for (auto *child : item->childItems()) {
                rows += child->objectName() == "groupWindow";
                count(child);
            }
        };
        count(groupList);
        return rows;
    };
    QTest::mouseMove(&view, centre(stack));
    if (!groupList || !QTest::qWaitFor([&] {
            return inPopover(groupList) && groupRows() == 2 && !popover->keyboard() &&
                   !view.rootObject()->property("menuOpen").toBool();
        })) {
        std::cerr << "hovering a stacked task did not list its windows "
                  << stack->property("hovered").toBool()
                  << view.rootObject()->property("groupOpen").toBool() << groupRows()
                  << groupList->isVisible() << popover->isVisible() << "\n";
        return 1;
    }
    // The popover takes the pointer over the list alone, and between it and the bar.
    {
        // Once it has slid into place.
        auto box = [&] {
            QRectF area = groupList->mapRectToScene(QRectF(0, 0, groupList->width(), groupList->height()));
            area.setBottom(popover->height() - view.height());
            return area;
        };
        if (!QTest::qWaitFor([&] { return popover->inputRegion() == QRegion(box().toAlignedRect()); })) {
            std::cerr << "the popover takes the pointer elsewhere than over the list: "
                      << QDebug::toString(popover->inputRegion()).toStdString() << " for "
                      << QDebug::toString(box()).toStdString() << '\n';
            return 1;
        }
    }
    // The pointer can cross from the button to the list without it closing, and choosing a
    // window there closes it.
    std::function<QQuickItem *(QQuickItem *)> firstRow = [&](QQuickItem *item) -> QQuickItem * {
        for (auto *child : item->childItems()) {
            if (child->objectName() == "groupWindow")
                return child;
            if (auto *found = firstRow(child))
                return found;
        }
        return nullptr;
    };
    // The pointer leaves the bar's surface and comes into the popover's.
    const QPoint row = centre(firstRow(groupList));
    QEvent leaveBar(QEvent::Leave);
    QCoreApplication::sendEvent(&view, &leaveBar);
    for (int step = 1; step <= 5; ++step) {
        QTest::mouseMove(popover, row + QPoint(0, 30 * (5 - step) / 5));
        QTest::qWait(10);
    }
    QTest::qWait(600);
    if (!groupList->isVisible()) {
        std::cerr << "moving from a stacked task to its windows hid them\n";
        return 1;
    }
    QTest::mouseClick(popover, Qt::LeftButton, Qt::NoModifier, row);
    if (!QTest::qWaitFor([&] { return !groupList->isVisible() && !popover->isVisible(); })) {
        std::cerr << "choosing one of a stacked task's windows did not hide them\n";
        return 1;
    }
    // Hovered again, the list goes once the pointer leaves.
    QEvent leavePopover(QEvent::Leave);
    QCoreApplication::sendEvent(popover, &leavePopover);
    QTest::mouseMove(&view, centre(stack));
    if (!QTest::qWaitFor([&] { return groupList->isVisible(); })) {
        std::cerr << "hovering a stacked task again did not list its windows\n";
        return 1;
    }
    QTest::mouseMove(&view, empty);
    if (!QTest::qWaitFor([&] { return !groupList->isVisible() && !popover->isVisible(); })) {
        std::cerr << "leaving a stacked task did not hide its windows\n";
        return 1;
    }
    // Dragging the stack moves all its windows together.
    {
        const QPoint from = centre(stack), to = centre(listedTask(0)) - QPoint(8, 0);
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, from);
        for (int step = 1; step <= 10; ++step) {
            QTest::mouseMove(&view, from + (to - from) * step / 10);
            QTest::qWait(10);
        }
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, to);
    }
    if (!QTest::qWaitFor([&] { return taskIdAt(0) == 10; }) || taskIdAt(1) != 11) {
        std::cerr << "dragging a stacked task did not move all its windows\n";
        return 1;
    }
    // One window left, the button is a plain one again.
    editTasks("model.remove(0)");
    if (!QTest::qWaitFor([&] {
            auto *single = listedTask(0);
            return single && single->property("taskId").toInt() == 11 &&
                   !single->property("stacked").toBool();
        })) {
        std::cerr << "a stacked task with one window left did not become a plain one\n";
        return 1;
    }
    // The volume control, fed by a stand-in sound server. Its popups open above it, inside
    // the panel's own surface.
    audio.update({"speakers",
                  {{"speakers", "Speakers", 50, false}, {"headset", "Headset", 30, false}},
                  {{41, "Music", "audio-x-generic", 80, false},
                   {42, "Browser", "audio-x-generic", 20, false}}});
    view.rootObject()->setProperty("audioSource", QVariant::fromValue<QObject *>(&audio));
    auto *volume = view.rootObject()->findChild<QQuickItem *>("audioWidget");
    // Laid out once shown: it sits after the task list, not at the bar's start.
    if (!volume || !QTest::qWaitFor([&] { return volume->isVisible() && volume->x() > 0; })) {
        std::cerr << "the volume control did not appear\n";
        return 1;
    }
    // Its tooltip says the volume, not the tooltip's own (empty) accessible name.
    bool tooltipText = false;
    for (auto *child : volume->children())
        if (child->property("text").toString().startsWith("Volume 50%"))
            tooltipText = true;
    if (!tooltipText) {
        std::cerr << "the volume control's tooltip does not show the volume\n";
        return 1;
    }
    auto wheel = [&](int delta) {
        QWheelEvent event(centre(volume), view.mapToGlobal(centre(volume)), {}, {0, delta},
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(&view, &event);
    };
    wheel(120);
    wheel(120);
    wheel(-120);
    if (audio.requests !=
            QStringList{"volume speakers 55", "volume speakers 60", "volume speakers 55"} ||
        audio.volume() != 55) {
        std::cerr << "scrolling on the volume control did not step the volume: "
                  << audio.requests.join(", ").toStdString() << '\n';
        return 1;
    }
    audio.requests.clear();
    // On the output, the bar's surface along the popover's bottom edge.
    auto above = [&](QQuickItem *popup) {
        if (auto *card = popup->property("card").value<QQuickItem *>())
            popup = card;
        auto box = popup->mapRectToScene(QRectF(0, 0, popup->width(), popup->height()));
        auto widget = volume->mapRectToScene(QRectF(0, 0, volume->width(), volume->height()))
                          .translated(0, popover->height() - view.height());
        return inPopover(popup) && box.bottom() <= widget.top() &&
               box.left() <= widget.center().x() && box.right() >= widget.center().x();
    };
    auto *outputs = find(view.rootObject(), "audioOutputs");
    auto *mixer = find(view.rootObject(), "audioMixer");
    click(volume, Qt::RightButton);
    if (!outputs || !QTest::qWaitFor([&] { return above(outputs); }) ||
        !view.rootObject()->property("menuOpen").toBool()) {
        std::cerr << "right-clicking the volume control did not show the outputs above it\n";
        return 1;
    }
    std::function<QQuickItem *(QQuickItem *, const QString &, const QString &)> findNamed =
        [&](QQuickItem *parent, const QString &name, const QString &text) -> QQuickItem * {
        for (auto *item : parent->childItems()) {
            if (item->objectName() == name && (text.isEmpty() || item->property("text") == text))
                return item;
            if (auto *found = findNamed(item, name, text))
                return found;
        }
        return nullptr;
    };
    auto *headset = findNamed(outputs, "audioOutputItem", "Headset");
    if (!headset)
        return fail("the headset is not listed among the audio outputs");
    click(headset);
    if (!QTest::qWaitFor([&] { return !view.rootObject()->property("menuOpen").toBool(); }) ||
        audio.requests != QStringList{"output headset 2"} || audio.output() != "headset") {
        std::cerr << "choosing an output did not switch to it: "
                  << audio.requests.join(", ").toStdString() << '\n';
        return 1;
    }
    audio.requests.clear();
    click(volume);
    if (!mixer || !QTest::qWaitFor([&] { return above(mixer); })) {
        std::cerr << "clicking the volume control did not show the mixer above it\n";
        return 1;
    }
    QQuickItem *streamSlider = nullptr;
    if (!QTest::qWaitFor(
            [&] { return (streamSlider = findNamed(mixer, "audioStreamSlider", {})); }))
        return fail("no stream slider in the mixer");
    // The first application's slider, clicked three quarters along.
    const auto track =
        streamSlider->mapRectToScene(QRectF(0, 0, streamSlider->width(), streamSlider->height()));
    QTest::mouseClick(popover, Qt::LeftButton, Qt::NoModifier,
                      QPointF(track.left() + track.width() * 0.75, track.center().y()).toPoint());
    if (audio.requests.size() != 1 || !audio.requests[0].startsWith("stream 41 ") ||
        std::abs(audio.requests[0].section(' ', 2).toInt() - 75) > 5 || !mixer->isVisible()) {
        std::cerr << "the mixer's slider did not set the application's volume: "
                  << audio.requests.join(", ").toStdString() << '\n';
        return 1;
    }
    // The server's next report updates the row in place rather than rebuilding it.
    audio.update({"headset",
                  {{"speakers", "Speakers", 55, false}, {"headset", "Headset", 30, false}},
                  {{41, "Music", "audio-x-generic", 74, false},
                   {42, "Browser", "audio-x-generic", 20, false}}});
    if (findNamed(mixer, "audioStreamSlider", {}) != streamSlider) {
        std::cerr << "an update rebuilt the mixer's sliders\n";
        return 1;
    }
    QTest::keyClick(mixer->window(), Qt::Key_Escape);
    if (!QTest::qWaitFor([&] { return !popover->isVisible(); })) {
        std::cerr << "the mixer did not close\n";
        return 1;
    }
    // The command palette: one search over actions, sessions, workspaces, windows and apps.
    {
        PaletteView paletteView(controller, app.primaryScreen());
        if (paletteView.status() != QQuickView::Ready) {
            for (const auto &error : paletteView.errors())
                std::cerr << error.toString().toStdString() << '\n';
            return 1;
        }
        auto palette = controller.palette();
        auto titlesNow = [&] {
            QStringList titles;
            for (const auto &item : palette->results())
                titles << item.toMap()["title"].toString();
            return titles;
        };
        auto openPalette = [&] {
            palette->open(output);
            return QTest::qWaitFor([&] { return paletteView.isVisible(); }) &&
                   QTest::qWaitFor([&] {
                       return find(paletteView.rootObject(), "paletteInput")->hasActiveFocus();
                   });
        };
        auto type = [&](const QString &text) {
            for (const auto &c : text)
                QTest::keyClick(&paletteView, c.toLatin1() ? static_cast<Qt::Key>(c.toUpper().unicode()) : Qt::Key_unknown,
                                Qt::NoModifier);
        };
        if (!openPalette()) {
            std::cerr << "the palette did not open with keyboard focus\n";
            return 1;
        }
        // Saved sessions arrive from the compositor after it opens.
        if (!QTest::qWaitFor([&] { return titlesNow().contains("Restore session work"); })) {
            std::cerr << "the palette lacks the saved session: " << titlesNow().join("|").toStdString() << '\n';
            return 1;
        }
        if (!titlesNow().contains("Workspace 1: web") || !titlesNow().contains("Fake app") ||
            !titlesNow().contains("Toggle tiling") || !titlesNow().contains("Appearance: dark")) {
            std::cerr << "the palette lacks workspaces, apps or actions\n";
            return 1;
        }
        type("tiling");
        if (!QTest::qWaitFor([&] { return !titlesNow().isEmpty() && titlesNow()[0].contains("tiling", Qt::CaseInsensitive); })) {
            std::cerr << "typing did not filter the palette: " << titlesNow().join("|").toStdString() << '\n';
            return 1;
        }
        QTest::keyClick(&paletteView, Qt::Key_Return);
        if (!QTest::qWaitFor([&] { return requests == QStringList{"toggle_tiling"}; }) ||
            !QTest::qWaitFor([&] { return !paletteView.isVisible(); }) || !palette->output().isEmpty()) {
            std::cerr << "running an action from the palette failed: " << requests.join("|").toStdString() << " results " << titlesNow().join("|").toStdString() << " q=" << palette->query().toStdString() << "\n";
            return 1;
        }
        // The arrows move the selection; a filter prefix narrows the kind.
        requests.clear();
        if (!openPalette()) {
            std::cerr << "the palette did not open again\n";
            return 1;
        }
        if (!palette->query().isEmpty()) {
            std::cerr << "the palette kept its last search\n";
            return 1;
        }
        type(">layoutmon");
        QTest::keyClick(&paletteView, Qt::Key_Down);
        QTest::keyClick(&paletteView, Qt::Key_Up);
        if (!QTest::qWaitFor([&] { return titlesNow().contains("Layout: monocle") && palette->selected() == 0; })) {
            std::cerr << "the action filter or the selection is wrong: " << titlesNow().join("|").toStdString() << '\n';
            return 1;
        }
        QTest::keyClick(&paletteView, Qt::Key_Return);
        if (!QTest::qWaitFor([&] { return requests == QStringList{"layout_monocle"}; })) {
            std::cerr << "the layout action was not sent\n";
            return 1;
        }
        requests.clear();
        if (!openPalette()) {
            std::cerr << "the palette did not open a third time\n";
            return 1;
        }
        type(">open terminal");
        QTest::keyClick(&paletteView, Qt::Key_Return);
        if (!QTest::qWaitFor([&] { return requests == QStringList{"terminal"}; })) {
            std::cerr << "the palette did not open a terminal: " << requests.join("|").toStdString()
                      << " results " << titlesNow().join("|").toStdString() << '\n';
            return 1;
        }
        // A late answer with the saved sessions does not move the selection off the entry the
        // user has reached.
        holdSessions = true;
        pendingSessions = nullptr;
        if (!openPalette() || !QTest::qWaitFor([&] { return pendingSessions != nullptr; })) {
            std::cerr << "the palette did not ask for the sessions\n";
            return 1;
        }
        QTest::keyClick(&paletteView, Qt::Key_Down);
        QTest::keyClick(&paletteView, Qt::Key_Down);
        QTest::keyClick(&paletteView, Qt::Key_Down);
        const auto reached = titlesNow().value(palette->selected());
        if (palette->selected() != 3 || reached.isEmpty()) {
            std::cerr << "the arrows did not move the selection\n";
            return 1;
        }
        pendingSessions->write("ok\nwork\t3\t1700000000\n");
        pendingSessions->disconnectFromServer();
        if (!QTest::qWaitFor([&] { return titlesNow().contains("Restore session work"); }) ||
            titlesNow().value(palette->selected()) != reached) {
            std::cerr << "the sessions arriving moved the selection from '"
                      << reached.toStdString() << "' to '"
                      << titlesNow().value(palette->selected()).toStdString() << "'\n";
            return 1;
        }
        holdSessions = false;
        palette->close();
        // A session restores by name, and the text typed can name a new one.
        requests.clear();
        openPalette();
        type("%work");
        QTest::keyClick(&paletteView, Qt::Key_Return);
        if (!QTest::qWaitFor([&] { return requests == QStringList{"session restore work"}; })) {
            std::cerr << "restoring a session from the palette failed: " << requests.join("|").toStdString() << '\n';
            return 1;
        }
        requests.clear();
        openPalette();
        type("evening");
        if (!QTest::qWaitFor([&] { return titlesNow().contains("Save session as evening"); })) {
            std::cerr << "the palette does not offer to save a session by the typed name\n";
            return 1;
        }
        QTest::keyClick(&paletteView, Qt::Key_Up); // wraps to the last: the save entry
        QTest::keyClick(&paletteView, Qt::Key_Return);
        if (!QTest::qWaitFor([&] { return requests == QStringList{"session save evening"}; })) {
            std::cerr << "saving a session from the palette failed: " << requests.join("|").toStdString() << '\n';
            return 1;
        }
        // A workspace switches the palette's own output.
        switches.clear();
        openPalette();
        type("#3");
        QTest::keyClick(&paletteView, Qt::Key_Return);
        if (!QTest::qWaitFor([&] { return switches == QStringList{"output " + output + " workspace 3"}; })) {
            std::cerr << "switching workspace from the palette failed: " << switches.join("|").toStdString() << '\n';
            return 1;
        }
        // Escape closes without running anything, and so does losing the keyboard.
        requests.clear();
        openPalette();
        type("tiling");
        QTest::keyClick(&paletteView, Qt::Key_Escape);
        if (!QTest::qWaitFor([&] { return !paletteView.isVisible(); }) || !requests.isEmpty()) {
            std::cerr << "Escape did not close the palette quietly\n";
            return 1;
        }
        // Its entries for another search (the start menu's): the windows of the model it is
        // given, no applications; one runs as it would from the palette.
        int windows = 0;
        bool apps = false, tiling = false;
        for (const auto &item : palette->entries(fakeModel)) {
            windows += item.toMap()["kind"] == "window";
            apps = apps || item.toMap()["kind"] == "app";
            tiling = tiling || item.toMap()["title"] == "Toggle tiling";
        }
        const int rows = qobject_cast<QAbstractItemModel *>(fakeModel)->rowCount();
        if (rows == 0 || windows != rows || apps || !tiling)
            return fail("the palette's entries for another search are not its windows and actions");
        palette->run({{"kind", "action"}, {"target", "toggle_tiling"}}, output);
        if (!QTest::qWaitFor([&] { return requests == QStringList{"toggle_tiling"}; }))
            return fail("an entry of the palette's did not run outside it");
        requests.clear();
        // An application it launches is among those launched lately.
        palette->run({{"kind", "app"}, {"target", "shaodesk-test-other.desktop"}}, output);
        if (!QTest::qWaitFor([&] {
                return controller.startMenu()->recent().value(0).toMap()["appId"] == "shaodesk-test-other.desktop";
            }))
            return fail("an application launched from the palette was not recorded");
    }
    // The start menu: its pinned applications and those launched lately, every application from
    // A to Z, and a search over applications, windows and actions, each moved through with the
    // keyboard; Escape clears the search, then closes the menu.
    {
        auto *start = controller.startMenu();
        auto *launcher = find(view.rootObject(), "launcher");
        auto *search = find(view.rootObject(), "applicationSearch");
        auto launcherOpen = [&] { return view.rootObject()->property("launcherOpen").toBool(); };
        auto openStart = [&] {
            view.rootObject()->setProperty("launcherOpen", true);
            return QTest::qWaitFor([&] { return inPopover(launcher) && search->hasActiveFocus(); });
        };
        auto item = [&](const QString &name) { return find(view.rootObject(), name); };
        auto shown = [&](const QString &name) { return item(name) && item(name)->isVisible(); };
        auto key = [&](Qt::Key key) { QTest::keyClick(popover, key); };
        auto type = [&](const QString &text) {
            for (const QChar c : text)
                QTest::keyClick(popover, c.toLatin1());
        };
        if (!launcher || !search)
            return fail("the start menu is missing");
        // With no pins of its own, it has the taskbar's, which are none here.
        if (!start->pinned().isEmpty() || QFile::exists(screens.filePath("state/shaodesk/start-pinned")))
            return fail("the start menu has pins of its own before any was made");
        for (const auto *id : {"shaodesk-test-app.desktop", "shaodesk-test-other.desktop",
                               "shaodesk-test-actions.desktop"})
            start->pin(id);
        if (!openStart() || !QTest::qWaitFor([&] { return shown("startTile:shaodesk-test-actions.desktop"); }) ||
            !shown("startTile:shaodesk-test-app.desktop") || !shown("startAllApps"))
            return fail("the start menu did not open on its pinned applications");
        // Down goes to the first tile, Right to the next, and Enter launches it, recorded among
        // those launched lately.
        key(Qt::Key_Down);
        if (!QTest::qWaitFor([&] { return item("startTile:shaodesk-test-app.desktop")->property("current").toBool(); }))
            return fail("Down did not go to the first pinned application");
        key(Qt::Key_Right);
        if (!QTest::qWaitFor([&] { return item("startTile:shaodesk-test-other.desktop")->property("current").toBool(); }) ||
            item("startTile:shaodesk-test-app.desktop")->property("current").toBool())
            return fail("Right did not go to the next pinned application");
        key(Qt::Key_Return);
        if (!QTest::qWaitFor([&] { return !launcherOpen(); }) || start->recent().isEmpty() ||
            start->recent()[0].toMap()["appId"] != "shaodesk-test-other.desktop")
            return fail("Enter did not launch the pinned application the keyboard was at");
        if (!openStart() || !QTest::qWaitFor([&] { return shown("startRecent:shaodesk-test-other.desktop"); }) ||
            item("startRecent:shaodesk-test-other.desktop")->property("subtitle") != "Just now" ||
            item("startTile:shaodesk-test-other.desktop")->property("current").toBool())
            return fail("the start menu does not list what was launched lately, or kept the keyboard's place");
        // All apps lists them by letter; a letter's heading shows the letters to jump to.
        click(item("startAllApps"));
        if (!QTest::qWaitFor([&] {
                return shown("startApp:shaodesk-test-other.desktop") && !shown("startTile:shaodesk-test-app.desktop");
            }) ||
            !shown("startApp:pinned:0"))
            return fail("All apps did not list every application");
        click(item("startLetter:O"));
        if (!QTest::qWaitFor([&] { return shown("startLetters") && item("startJump:F")->isEnabled(); }) ||
            item("startJump:Q")->isEnabled())
            return fail("a letter's heading did not show the letters to jump to");
        click(item("startJump:F"));
        if (!QTest::qWaitFor([&] { return !shown("startLetters"); }))
            return fail("jumping to a letter did not show the list again");
        click(item("startLetter:O"));
        if (!QTest::qWaitFor([&] { return shown("startLetters"); }))
            return fail("a letter's heading did not show the letters again");
        key(Qt::Key_Escape);
        if (!QTest::qWaitFor([&] { return !shown("startLetters"); }) || !launcherOpen() ||
            !shown("startApp:shaodesk-test-other.desktop"))
            return fail("Escape did not close only the letters");
        key(Qt::Key_Down);
        key(Qt::Key_Down);
        if (!QTest::qWaitFor([&] { return item("startApp:shaodesk-test-app.desktop")->property("current").toBool(); }))
            return fail("Down did not move through All apps past the letters' headings");
        click(item("startBack"));
        if (!QTest::qWaitFor([&] { return shown("startTile:shaodesk-test-app.desktop"); }))
            return fail("Back did not show the pinned applications again");
        // An application's menu, from a right press on its tile or row: Open, its desktop
        // actions, pinning to the start menu and the taskbar, and a tile's Move to front.
        auto *appMenu = item("startAppMenu");
        auto menuFor = [&](const QString &name) {
            click(item(name), Qt::RightButton);
            return QTest::qWaitFor([&] { return inPopover(appMenu) && shown("startMenu:open"); });
        };
        auto choose = [&](const QString &entry) {
            if (!shown(entry))
                return false;
            click(item(entry));
            return QTest::qWaitFor([&] { return !appMenu->isVisible(); });
        };
        auto startPins = [&] {
            QFile file(screens.filePath("state/shaodesk/start-pinned"));
            return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
        };
        if (!appMenu || !menuFor("startTile:shaodesk-test-other.desktop") || !shown("startMenu:unpin") ||
            !shown("startMenu:front") || item("startMenu:taskbar")->property("text") != "Pin to taskbar" ||
            shown("startMenu:pin"))
            return fail("a pinned tile's menu does not offer to unpin it, move it to the front and pin it to the taskbar");
        if (!choose("startMenu:taskbar") || !QTest::qWaitFor([&] { return controller.isPinned("shaodesk-test-other.desktop"); }) ||
            !launcherOpen() || !search->hasActiveFocus())
            return fail("Pin to taskbar from the start menu did not pin it, or closed more than the menu");
        if (!menuFor("startTile:shaodesk-test-other.desktop") ||
            item("startMenu:taskbar")->property("text") != "Unpin from taskbar" || !choose("startMenu:taskbar") ||
            !QTest::qWaitFor([&] { return !controller.isPinned("shaodesk-test-other.desktop"); }))
            return fail("Unpin from taskbar from the start menu did not unpin it");
        if (!menuFor("startTile:shaodesk-test-other.desktop") || !choose("startMenu:front") ||
            !QTest::qWaitFor([&] {
                return startPins() == "shaodesk-test-other.desktop\nshaodesk-test-app.desktop\n"
                                      "shaodesk-test-actions.desktop\n";
            }))
            return fail(("Move to front did not move the tile first: " + startPins().toStdString()).c_str());
        if (!menuFor("startTile:shaodesk-test-other.desktop") || shown("startMenu:front") ||
            !choose("startMenu:unpin") ||
            !QTest::qWaitFor([&] { return !item("startTile:shaodesk-test-other.desktop"); }) ||
            startPins() != "shaodesk-test-app.desktop\nshaodesk-test-actions.desktop\n")
            return fail("Unpin from Start did not remove the tile and forget it");
        // A row of All apps pins to the start menu; a configured launcher is only opened.
        click(item("startAllApps"));
        if (!QTest::qWaitFor([&] { return shown("startApp:shaodesk-test-other.desktop"); }) ||
            !menuFor("startApp:shaodesk-test-other.desktop") || shown("startMenu:unpin") || shown("startMenu:front") ||
            !choose("startMenu:pin") ||
            !QTest::qWaitFor([&] { return startPins().endsWith("shaodesk-test-other.desktop\n"); }))
            return fail("Pin to Start from All apps did not pin it at the end");
        if (!menuFor("startApp:pinned:0") || shown("startMenu:pin") || shown("startMenu:taskbar"))
            return fail("a configured launcher's menu offers to pin it");
        key(Qt::Key_Escape);
        if (!QTest::qWaitFor([&] { return !appMenu->isVisible() && search->hasActiveFocus(); }) || !launcherOpen())
            return fail("Escape in an application's menu did not close only the menu");
        click(item("startBack"));
        // The menu key opens the menu of what the keyboard is at, its first entry highlighted;
        // a desktop action runs from it.
        key(Qt::Key_Down);
        key(Qt::Key_Right);
        QTest::keyClick(popover, Qt::Key_Menu);
        if (!QTest::qWaitFor([&] {
                return inPopover(appMenu) && shown("startMenu:action:touch") &&
                       item("startMenu:open")->property("highlighted").toBool();
            }))
            return fail("the menu key did not open the menu of the tile the keyboard was at");
        QFile::remove(actionMarker);
        if (!choose("startMenu:action:touch") || !QTest::qWaitFor([&] { return QFile::exists(actionMarker); }) ||
            !QTest::qWaitFor([&] { return !launcherOpen(); }))
            return fail("a desktop action from the start menu did not run, or the menu stayed");
        if (!openStart())
            return fail("the start menu did not open after a desktop action");
        // Dragging a tile onto another's place moves it there, the press launching nothing.
        {
            auto *dragged = item("startTile:shaodesk-test-other.desktop");
            auto *first = item("startTile:shaodesk-test-app.desktop");
            if (!dragged || !first || !QTest::qWaitFor([&] { return dragged->isVisible() && first->isVisible(); }))
                return fail("the tiles to drag are not shown");
            const QPoint from = centre(dragged), to = centre(first);
            QTest::mousePress(popover, Qt::LeftButton, Qt::NoModifier, from);
            for (int step = 1; step <= 10; ++step) {
                QTest::mouseMove(popover, from + (to - from) * step / 10);
                QTest::qWait(10);
            }
            QTest::mouseRelease(popover, Qt::LeftButton, Qt::NoModifier, to);
            if (!QTest::qWaitFor([&] {
                    return startPins() == "shaodesk-test-other.desktop\nshaodesk-test-app.desktop\n"
                                          "shaodesk-test-actions.desktop\n";
                }) ||
                !launcherOpen() || start->recent()[0].toMap()["appId"] == "shaodesk-test-other.desktop")
                return fail(("dragging a tile did not move it: " + startPins().toStdString()).c_str());
        }
        // A search groups what it finds: the best match first, then applications, windows and
        // actions; the keyboard moves through them all and Enter runs the one it is at.
        editTasks("model.append({ taskId: 42, title: 'Quarterly report', appId: 'shaodesk-test-other', "
                  "active: false, minimized: false, urgent: false })");
        type("quarterly");
        if (!QTest::qWaitFor([&] { return shown("startBestMatch"); }) ||
            item("startBestMatch")->property("result").toMap()["title"] != "Quarterly report" ||
            item("startBestOpen")->property("text") != "Switch to")
            return fail("searching for a window's title did not find it as the best match");
        search->setProperty("text", "");
        type("zqxw");
        if (!QTest::qWaitFor([&] { return shown("startNothing"); }) || shown("startBestMatch"))
            return fail("a search that finds nothing does not say so");
        search->setProperty("text", "");
        type("action");
        if (!QTest::qWaitFor([&] { return shown("startBestMatch") && shown("startBestAction:touch"); }) ||
            item("startBestMatch")->property("result").toMap()["title"] != "Action app")
            return fail("an application found as the best match does not offer its desktop actions");
        // The keyboard reaches the best match's buttons: Right or Tab moves on to Open and then
        // its actions, Left back; Enter presses the one it is at.
        auto atButton = [&](const QString &name) { return item(name)->property("current").toBool(); };
        key(Qt::Key_Right);
        if (!QTest::qWaitFor([&] { return atButton("startBestOpen"); }))
            return fail("Right did not move from the best match to its Open button");
        key(Qt::Key_Tab);
        if (!QTest::qWaitFor([&] { return atButton("startBestAction:touch"); }) || atButton("startBestOpen"))
            return fail("Tab did not move on to the best match's first action");
        key(Qt::Key_Left);
        if (!QTest::qWaitFor([&] { return atButton("startBestOpen"); }) || atButton("startBestAction:touch"))
            return fail("Left did not move back to the best match's Open button");
        key(Qt::Key_Right);
        QFile::remove(actionMarker);
        key(Qt::Key_Return);
        if (!QTest::qWaitFor([&] { return QFile::exists(actionMarker); }) ||
            !QTest::qWaitFor([&] { return !launcherOpen(); }))
            return fail("Enter did not run the best match's action the keyboard was at");
        if (!openStart())
            return fail("the start menu did not open after running the best match's action");
        search->setProperty("text", "");
        type("app");
        // Every application's name has it (and "Applications menu", an action): the model says
        // which is best.
        const auto apps = start->search("app", controller.palette()->entries(fakeModel));
        const auto best = apps.value(0).toMap()["title"].toString(), next = apps.value(1).toMap()["title"].toString();
        if (apps.size() < 4 || apps[1].toMap()["group"] != "apps" ||
            !QTest::qWaitFor([&] { return shown("startBestMatch") && shown("startResult:" + next); }) ||
            item("startBestMatch")->property("result").toMap()["title"] != best)
            return fail("searching for applications did not list them under the best match");
        key(Qt::Key_Down);
        if (!QTest::qWaitFor([&] { return item("startResult:" + next)->property("current").toBool(); }) ||
            item("startBestMatch")->property("current").toBool())
            return fail("Down did not move from the best match to the next result");
        key(Qt::Key_Up);
        if (!QTest::qWaitFor([&] { return item("startBestMatch")->property("current").toBool(); }))
            return fail("Up did not move back to the best match");
        // Moving the pointer onto a result chooses it. (Results appearing under the pointer
        // where it rests do not: the best match stayed chosen above, and below.)
        QTest::mouseMove(popover, centre(item("startResult:" + next)) + QPoint(0, 2));
        QTest::mouseMove(popover, centre(item("startResult:" + next)));
        if (!QTest::qWaitFor([&] { return item("startResult:" + next)->property("current").toBool(); }))
            return fail("moving the pointer onto a result did not choose it");
        // The field's cross clears the search, the keyboard staying there.
        click(item("startClear"));
        if (!QTest::qWaitFor([&] { return search->property("text").toString().isEmpty() && !shown("startClear"); }) ||
            !search->hasActiveFocus() || !launcherOpen())
            return fail("the search field's cross did not clear it");
        type("app");
        // Escape clears the search, then closes the menu.
        key(Qt::Key_Escape);
        if (!QTest::qWaitFor([&] {
                return search->property("text").toString().isEmpty() && shown("startTile:shaodesk-test-app.desktop");
            }) ||
            !launcherOpen())
            return fail("Escape did not clear the search first");
        key(Qt::Key_Escape);
        if (!QTest::qWaitFor([&] { return !launcherOpen(); }))
            return fail("Escape did not close the start menu once the search was clear");
        // An action found runs as from the palette.
        requests.clear();
        if (!openStart())
            return fail("the start menu did not open again");
        type("toggle tiling");
        if (!QTest::qWaitFor([&] {
                return shown("startBestMatch") &&
                       item("startBestMatch")->property("result").toMap()["title"] == "Toggle tiling";
            }))
            return fail("the start menu's search did not find an action");
        key(Qt::Key_Return);
        if (!QTest::qWaitFor([&] { return requests == QStringList{"toggle_tiling"} && !launcherOpen(); }))
            return fail("Enter did not run the action the start menu found");
        requests.clear();
        // More pins than a page holds go on pages, which the wheel, the dots beside them and the
        // keyboard moving past the last row turn.
        {
            QStringList extra;
            for (int i = 0; i < 20; ++i) {
                extra << QString("shaodesk-test-page%1.desktop").arg(i);
                QFile entry(screens.filePath("data/applications/" + extra.last()));
                if (!entry.open(QIODevice::WriteOnly) ||
                    entry.write(QString("[Desktop Entry]\nType=Application\nName=Page app %1\nExec=true\n")
                                    .arg(i, 2, 10, QChar('0'))
                                    .toUtf8()) < 0)
                    return fail("could not write an application to pin");
            }
            auto installed = [&](const QString &id) {
                const auto apps = controller.apps();
                return std::any_of(apps.begin(), apps.end(),
                                   [&](const QVariant &app) { return app.toMap()["appId"] == id; });
            };
            if (!QTest::qWaitFor([&] { return installed(extra.last()); }, 10000))
                return fail("the applications to pin were not found");
            for (const auto &id : extra)
                start->pin(id);
            auto *home = item("startHome");
            if (!openStart() || !QTest::qWaitFor([&] { return shown("startPage1"); }) ||
                home->property("page").toInt() != 0)
                return fail("more pins than a page holds did not go on pages");
            auto *pinned = item("startPinned");
            const QPoint over = centre(pinned);
            QWheelEvent wheel(over, popover->mapToGlobal(over), QPoint(), QPoint(0, -120), Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
            QCoreApplication::sendEvent(popover, &wheel);
            if (!QTest::qWaitFor([&] { return home->property("page").toInt() == 1; }))
                return fail("the wheel did not turn the pins' page");
            click(item("startPage0"));
            if (!QTest::qWaitFor([&] { return home->property("page").toInt() == 0; }))
                return fail("a page's dot did not show its page");
            for (int row = 0; row < 4; ++row)
                key(Qt::Key_Down);
            if (!QTest::qWaitFor([&] { return home->property("page").toInt() == 1; }) ||
                home->property("current").toInt() != 3 * home->property("columns").toInt())
                return fail("Down past the last row did not go on to the next page");
            view.rootObject()->setProperty("launcherOpen", false);
            for (const auto &id : extra) {
                start->unpin(id);
                QFile::remove(screens.filePath("data/applications/" + id));
            }
            if (!QTest::qWaitFor([&] { return !installed(extra.last()); }, 10000))
                return fail("the pinned applications were not removed again");
        }
        // Along its bottom, the user's name and picture.
        start->setUser("Robin Lee", QUrl::fromLocalFile(walls + "/a/one.png"));
        if (!openStart() || !QTest::qWaitFor([&] { return shown("userPicture"); }) ||
            item("userName")->property("text") != "Robin Lee")
            return fail("the start menu does not show the user's name and picture");
        view.rootObject()->setProperty("launcherOpen", false);
        editTasks("model.remove(model.count - 1)");
    }
    // The power menu, from the power button in the launcher's bottom-right corner, lists what
    // the compositor says may run, and runs it.
    {
        auto *button = find(view.rootObject(), "powerButton");
        auto *menu = find(view.rootObject(), "powerMenu");
        auto item = [&](const QString &action) {
            return find(view.rootObject(), "powerItem:" + action);
        };
        auto openLauncher = [&] {
            if (!view.rootObject()->property("launcherOpen").toBool())
                QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, start);
            return QTest::qWaitFor([&] { return view.rootObject()->property("launcherOpen").toBool(); });
        };
        if (!button || !menu || !openLauncher() || button->isVisible()) {
            std::cerr << "the power button shows before the compositor said what may run\n";
            return 1;
        }
        subscriber->write("power lock,suspend,reboot,poweroff,logout\n");
        if (!QTest::qWaitFor([&] { return button->isVisible(); })) {
            std::cerr << "the power button did not appear\n";
            return 1;
        }
        // The footer's layout places the button on its next polish.
        if (!QTest::qWaitFor([&] {
                auto *launcher = find(view.rootObject(), "launcher");
                const auto corner = button->mapRectToScene(QRectF(0, 0, button->width(), button->height()));
                const auto area = launcher->mapRectToScene(QRectF(0, 0, launcher->width(), launcher->height()));
                return corner.right() >= area.right() - 40 && corner.bottom() >= area.bottom() - 40;
            }))
            return fail("the power button is not in the launcher's bottom-right corner");
        auto openMenu = [&] {
            if (!openLauncher())
                return false;
            click(button);
            return QTest::qWaitFor([&] {
                return inPopover(menu);
            });
        };
        if (!openMenu() || !item("lock") || !item("suspend") || item("hibernate")) {
            std::cerr << "the power menu does not list what may run\n";
            return 1;
        }
        // A press beside the menu closes it and leaves the launcher open; the button toggles it.
        click(find(view.rootObject(), "applicationSearch"));
        if (!QTest::qWaitFor([&] { return !menu->isVisible(); }) ||
            !view.rootObject()->property("launcherOpen").toBool())
            return fail("a press beside the power menu did not close only the power menu");
        if (!openMenu())
            return fail("the power menu did not open again");
        click(button);
        if (!QTest::qWaitFor([&] { return !menu->isVisible(); }))
            return fail("the power button did not close its menu");
        if (!openMenu())
            return fail("the power menu did not open a third time");
        requests.clear();
        click(item("suspend"));
        if (!QTest::qWaitFor([&] {
                return requests == QStringList{"suspend"} && !menu->isVisible() &&
                       !view.rootObject()->property("launcherOpen").toBool();
            })) {
            std::cerr << "suspending from the power menu failed: " << requests.join("|").toStdString()
                      << '\n';
            return 1;
        }
        // Restart, power off and log out ask first, on the panel's output: a dialog counts down
        // and goes ahead when it runs out, on its button or on Enter; Escape, Cancel or a click
        // beside it gives up.
        PowerView dialog(controller, app.primaryScreen());
        if (dialog.status() != QQuickView::Ready) {
            for (const auto &error : dialog.errors())
                std::cerr << error.toString().toStdString() << '\n';
            return 1;
        }
        auto *power = controller.power();
        auto ask = [&](const QString &action) {
            if (!openMenu() || !item(action))
                return false;
            click(item(action));
            return QTest::qWaitFor([&] {
                return dialog.isVisible() && power->pending() == action && !menu->isVisible();
            });
        };
        auto gaveUp = [&] {
            return QTest::qWaitFor([&] { return !dialog.isVisible() && power->pending().isEmpty(); });
        };
        auto dialogItem = [&](const char *name) { return find(dialog.rootObject(), name); };
        requests.clear();
        if (!ask("poweroff") || power->countdown() != 2 || power->output() != output ||
            power->pendingTitle() != "Power off" ||
            power->message() != "The computer powers off in 2 seconds." || !requests.isEmpty() ||
            dialogItem("powerMessage")->property("text") != power->message()) {
            std::cerr << "power off did not ask first: " << power->message().toStdString() << '\n';
            return 1;
        }
        QTest::keyClick(&dialog, Qt::Key_Escape);
        if (!gaveUp()) {
            std::cerr << "Escape did not give up the power off\n";
            return 1;
        }
        if (!ask("reboot") || power->message() != "The computer restarts in 2 seconds.")
            return fail("the restart was not asked about");
        QTest::mouseClick(&dialog, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
        if (!gaveUp()) {
            std::cerr << "a click beside the dialog did not give up the restart\n";
            return 1;
        }
        if (!ask("logout"))
            return fail("the log out was not asked about");
        QTest::mouseClick(&dialog, Qt::LeftButton, Qt::NoModifier, centre(dialogItem("powerCancel")));
        if (!gaveUp()) {
            std::cerr << "Cancel did not give up the log out\n";
            return 1;
        }
        if (!ask("reboot"))
            return fail("the restart was not asked about again");
        QTest::mouseClick(&dialog, Qt::LeftButton, Qt::NoModifier, centre(dialogItem("powerConfirm")));
        if (!QTest::qWaitFor([&] { return requests == QStringList{"reboot"}; }) || !gaveUp()) {
            std::cerr << "the restart button did not restart: " << requests.join("|").toStdString()
                      << '\n';
            return 1;
        }
        requests.clear();
        if (!ask("logout"))
            return fail("the log out was not asked about again");
        QTest::keyClick(&dialog, Qt::Key_Return);
        if (!QTest::qWaitFor([&] { return requests == QStringList{"logout"}; }) || !gaveUp()) {
            std::cerr << "Enter did not log out\n";
            return 1;
        }
        // The keyboard starts on the action's button; Tab takes it to Cancel, where Enter gives
        // up.
        requests.clear();
        if (!ask("reboot") || !dialogItem("powerConfirm")->hasActiveFocus())
            return fail("the power dialog's keyboard is not on its action's button");
        QTest::keyClick(&dialog, Qt::Key_Tab);
        if (!QTest::qWaitFor([&] { return dialogItem("powerCancel")->hasActiveFocus(); }))
            return fail("Tab did not take the power dialog's keyboard to Cancel");
        QTest::keyClick(&dialog, Qt::Key_Return);
        if (!gaveUp() || !requests.isEmpty())
            return fail("Enter on Cancel did not give up the restart");
        requests.clear();
        if (!ask("poweroff"))
            return fail("the power off was not asked about");
        if (!QTest::qWaitFor([&] {
                return power->countdown() == 1 &&
                       power->message() == "The computer powers off in 1 second.";
            }) ||
            !requests.isEmpty() ||
            !QTest::qWaitFor([&] { return requests == QStringList{"poweroff"}; }) || !gaveUp()) {
            std::cerr << "the countdown did not run out into a power off: "
                      << requests.join("|").toStdString() << '\n';
            return 1;
        }
        // An action that may no longer run is not asked about any more.
        if (!ask("reboot"))
            return fail("the restart was not asked about a third time");
        subscriber->write("power lock,suspend,poweroff,logout\n");
        if (!gaveUp() || !requests.contains("poweroff") || requests.size() != 1) {
            std::cerr << "a restart that may no longer run was still asked about\n";
            return 1;
        }
        subscriber->write("power lock,suspend,reboot,poweroff,logout\n");
        // The command palette offers the same, and asks first the same way.
        {
            auto *palette = controller.palette();
            auto titles = [&] {
                QStringList found;
                for (const auto &result : palette->results())
                    found << result.toMap()["title"].toString();
                return found;
            };
            if (!QTest::qWaitFor([&] { return power->available().size() == 5; }))
                return fail("the power menu did not list five actions");
            palette->open(output);
            if (!QTest::qWaitFor([&] {
                    return titles().contains("Lock screen") && titles().contains("Suspend") &&
                           titles().contains("Restart…") && titles().contains("Power off…") &&
                           titles().contains("Log out…");
                }) ||
                titles().contains("Hibernate")) {
                std::cerr << "the palette does not offer what may run: "
                          << titles().join("|").toStdString() << '\n';
                return 1;
            }
            requests.clear();
            palette->setQuery(">power off");
            palette->activate(0);
            if (!QTest::qWaitFor([&] { return dialog.isVisible() && power->pending() == "poweroff"; }) ||
                !requests.isEmpty()) {
                std::cerr << "power off from the palette did not ask first\n";
                return 1;
            }
            power->cancel();
            palette->open(output);
            palette->setQuery("lock screen");
            palette->activate(0);
            if (!QTest::qWaitFor([&] { return requests == QStringList{"lock"}; }) ||
                !power->pending().isEmpty()) {
                std::cerr << "locking from the palette failed: " << requests.join("|").toStdString()
                          << '\n';
                return 1;
            }
        }
        // The power_menu action opens the launcher with the menu up and the keyboard in it: the
        // arrows choose, Enter runs, and asking again closes both.
        requests.clear();
        subscriber->write(("power-menu " + output + "\n").toUtf8());
        if (!QTest::qWaitFor([&] { return menu->isVisible() && menu->hasActiveFocus(); })) {
            std::cerr << "power_menu did not open the power menu with the keyboard\n";
            return 1;
        }
        subscriber->write(("power-menu " + output + "\n").toUtf8());
        if (!QTest::qWaitFor([&] {
                return !menu->isVisible() && !view.rootObject()->property("launcherOpen").toBool();
            })) {
            std::cerr << "power_menu again did not close the power menu\n";
            return 1;
        }
        subscriber->write(("power-menu " + output + "\n").toUtf8());
        if (!QTest::qWaitFor([&] { return menu->isVisible() && menu->hasActiveFocus(); }))
            return fail("the power menu did not open from the compositor");
        QTest::keyClick(menu->window(), Qt::Key_Down);
        QTest::keyClick(menu->window(), Qt::Key_Down);
        QTest::keyClick(menu->window(), Qt::Key_Up);
        QTest::keyClick(menu->window(), Qt::Key_Return);
        if (!QTest::qWaitFor([&] { return requests == QStringList{"suspend"} && !menu->isVisible(); })) {
            std::cerr << "the keyboard did not run the chosen power action: "
                      << requests.join("|").toStdString() << '\n';
            return 1;
        }
        // What the compositor refuses, at once or later, shows across the panel.
        powerRefusal = "no screen locker: power.lock_command is not set";
        if (!openMenu())
            return fail("the power menu did not open for the refusal");
        click(item("lock"));
        if (!QTest::qWaitFor([&] {
                return controller.error() ==
                       "Lock screen: no screen locker: power.lock_command is not set";
            })) {
            std::cerr << "a refused power action was not reported: "
                      << controller.error().toStdString() << '\n';
            return 1;
        }
        powerRefusal.clear();
        subscriber->write("power-error Suspend cancelled: the screen did not lock within 5 seconds\n");
        if (!QTest::qWaitFor([&] {
                return controller.error() ==
                       "Suspend cancelled: the screen did not lock within 5 seconds";
            })) {
            std::cerr << "a power action cancelled later was not reported\n";
            return 1;
        }
        subscriber->write("spawn-error Cannot launch kitty: No such file or directory\n");
        if (!QTest::qWaitFor([&] {
                return controller.error() == "Cannot launch kitty: No such file or directory";
            })) {
            std::cerr << "a program that did not start was not reported\n";
            return 1;
        }
        controller.clearError();
        // With nothing that may run, the button goes.
        if (!openLauncher() || !button->isVisible())
            return fail("the power button was gone before nothing could run");
        subscriber->write("power -\n");
        if (!QTest::qWaitFor([&] { return !button->isVisible(); })) {
            std::cerr << "the power button stayed with nothing to offer\n";
            return 1;
        }
        view.rootObject()->setProperty("launcherOpen", false);
        subscriber->write("power lock,suspend,reboot,poweroff,logout\n");
    }
    // Notifications: the bell, the cards, their buttons and the history, fed by hand the way the
    // D-Bus service feeds them.
    {
        auto *daemon = controller.notifications();
        auto *bell = find(view.rootObject(), "notificationBell");
        if (!bell || bell->isVisible()) {
            std::cerr << "the bell shows without a notification daemon\n";
            return 1;
        }
        daemon->setServing(true);
        // The bell is off unless shell.widgets.notifications asks for it: the clock does its work.
        QTest::qWait(50);
        if (bell->isVisible() || controller.widgets()["notifications"].toString() != "quick" ||
            !rewrite(QString(lua).replace("widgets={", "widgets={notifications='bar',")))
            return fail("the bell showed by default, or the configuration could not be rewritten");
        controller.reload();
        if (!QTest::qWaitFor([&] { return bell->isVisible() && bell->x() > 0; })) {
            std::cerr << "the bell did not appear once the daemon served and the setting asked\n";
            return 1;
        }
        CardsView cards(controller, app.primaryScreen());
        if (cards.status() != QQuickView::Ready) {
            for (const auto &error : cards.errors())
                std::cerr << error.toString().toStdString() << '\n';
            return 1;
        }
        if (cards.isVisible()) {
            std::cerr << "the cards' surface is up with nothing to show\n";
            return 1;
        }
        QSignalSpy invoked(daemon, &NotificationCenter::actionInvoked);
        QSignalSpy closed(daemon, &NotificationCenter::closed);
        auto make = [&](const QString &summary, bool actions) {
            Notification n;
            n.app = "Test";
            n.summary = summary;
            n.body = "with <b>bold</b>";
            n.timeout = 60000;
            if (actions)
                n.actions = {{"default", "Open"}, {"yes", "Yes"}};
            return daemon->notify(n);
        };
        auto card = [&]() { return find(cards.rootObject(), "notificationCard"); };
        make("Hello", true);
        if (!QTest::qWaitFor([&] { return cards.isVisible() && card() && card()->height() > 20; }) ||
            controller.cardsOutput() != output) {
            std::cerr << "a notification did not raise a card on its output\n";
            return 1;
        }
        // Top right of the output, inside the surface, whose width is the card and its margins.
        const auto box = card()->mapRectToScene(QRectF(0, 0, card()->width(), card()->height()));
        if (box.right() > cards.width() || box.left() < 0 || box.width() < 300) {
            std::cerr << "the card is not laid out inside its surface\n";
            return 1;
        }
        if (auto *badge = find(view.rootObject(), "notificationBadge");
            !badge || !QTest::qWaitFor([&] { return badge->isVisible(); })) {
            std::cerr << "the bell has no badge for an unread notification\n";
            return 1;
        }
        if (auto *badge = find(view.rootObject(), "clockBadge");
            !badge || !QTest::qWaitFor([&] { return badge->isVisible(); })) {
            std::cerr << "the clock has no badge for an unread notification\n";
            return 1;
        }
        // (Hovering a card holds its timer; offscreen Qt sends no hover to these items, so
        // notifications_smoke checks that with a real pointer.)
        // An action's button runs it and dismisses the card; the surface goes once it has left.
        auto *button = find(cards.rootObject(), "notificationAction");
        if (!button || button->property("text").toString() != "Yes") {
            std::cerr << "the card has no button for its action (and none for the default one)\n";
            return 1;
        }
        QTest::mouseClick(&cards, Qt::LeftButton, Qt::NoModifier, centre(button));
        if (!QTest::qWaitFor([&] { return invoked.count() == 1; }) ||
            invoked.at(0).at(1).toString() != "yes" || closed.count() != 1 ||
            closed.at(0).at(1).toUInt() != NotificationCenter::Dismissed) {
            std::cerr << "clicking an action's button did not run it and dismiss the card\n";
            return 1;
        }
        if (!QTest::qWaitFor([&] { return !cards.isVisible(); }, 3000)) {
            std::cerr << "the cards' surface stayed up after the last card left\n";
            return 1;
        }
        // A click on the card runs its default action.
        invoked.clear();
        closed.clear();
        make("Again", true);
        if (!QTest::qWaitFor([&] { return cards.isVisible() && card() && card()->height() > 20; })) {
            std::cerr << "another notification did not raise a card\n";
            return 1;
        }
        QTest::qWait(300); // the slide in
        QTest::mouseClick(&cards, Qt::LeftButton, Qt::NoModifier,
                          card()->mapToScene(QPointF(card()->width() - 60, 12)).toPoint());
        if (!QTest::qWaitFor([&] { return invoked.count() == 1; }) ||
            invoked.at(0).at(1).toString() != "default") {
            std::cerr << "clicking a card did not run its default action\n";
            return 1;
        }
        if (!QTest::qWaitFor([&] { return !cards.isVisible(); }, 3000)) {
            std::cerr << "the clicked card stayed\n";
            return 1;
        }
        // The close button dismisses without running anything.
        invoked.clear();
        closed.clear();
        make("Quiet", false);
        if (!QTest::qWaitFor([&] { return cards.isVisible() && card() && card()->height() > 20; })) {
            std::cerr << "a second notification did not raise a card\n";
            return 1;
        }
        QTest::qWait(300);
        auto *close = find(cards.rootObject(), "notificationClose");
        QTest::mouseClick(&cards, Qt::LeftButton, Qt::NoModifier, centre(close));
        if (!QTest::qWaitFor([&] { return closed.count() == 1; }) || invoked.count() != 0) {
            std::cerr << "the close button did not dismiss the card\n";
            return 1;
        }
        if (!QTest::qWaitFor([&] { return !cards.isVisible(); }, 3000)) {
            std::cerr << "the dismissed card stayed\n";
            return 1;
        }
        // A card's countdown line runs down with its timer, and both stop while the pointer is
        // on the card.
        {
            Notification n;
            n.app = "Test";
            n.summary = "Counting down";
            n.timeout = 5000;
            const uint id = daemon->notify(n);
            QQuickItem *line = nullptr;
            if (!QTest::qWaitFor([&] {
                    line = find(cards.rootObject(), "notificationCountdown");
                    return cards.isVisible() && card() && line && line->isVisible() &&
                           line->width() > 0;
                }))
                return fail("a card with a timeout has no countdown line");
            const qreal start = line->width();
            if (!QTest::qWaitFor([&] { return line->width() < start - 2; }))
                return fail("a card's countdown line does not run down");
            QTest::mouseMove(&cards, centre(card()));
            if (!QTest::qWaitFor([&] { return !daemon->timerRunning(id); }))
                return fail("the pointer on a card did not hold its timer");
            const qreal held = line->width();
            QTest::qWait(300);
            if (line->width() != held) {
                std::cerr << "a card's countdown line ran on while the pointer held it: " << held
                          << " then " << line->width() << '\n';
                return 1;
            }
            QTest::mouseMove(&cards, QPoint(1, 1));
            if (!QTest::qWaitFor(
                    [&] { return daemon->timerRunning(id) && line->width() < held - 2; }))
                return fail("a card's countdown did not run on once the pointer left it");
            daemon->dismiss(id);
            if (!QTest::qWaitFor([&] { return !cards.isVisible(); }, 3000))
                return fail("the counting card stayed");
        }
        // The history opens from the bell, marks what it shows as seen, and clears.
        auto *history = find(view.rootObject(), "notificationHistory");
        if (!history || history->isVisible()) {
            std::cerr << "the history is missing or open at start\n";
            return 1;
        }
        make("Kept one", true);
        make("Kept two", false);
        click(bell);
        if (!QTest::qWaitFor([&] { return history->isVisible(); }) || daemon->unread() != 0) {
            std::cerr << "clicking the bell did not open the history and mark it read\n";
            return 1;
        }
        // One application's notifications, under one heading, a row each.
        auto *list = find(view.rootObject(), "notificationList");
        std::function<QList<QQuickItem *>(QQuickItem *, const QString &)> findAll =
            [&](QQuickItem *item, const QString &name) {
                QList<QQuickItem *> found;
                if (item->objectName() == name)
                    found << item;
                for (auto *child : item->childItems())
                    found << findAll(child, name);
                return found;
            };
        auto rows = [&] { return list ? findAll(list, "notificationRow") : QList<QQuickItem *>(); };
        if (!list || daemon->history()->count() < 3 || !QTest::qWaitFor([&] {
                return list->property("count").toInt() == 1 && rows().size() == 2;
            })) {
            std::cerr << "the history does not list the notifications by application, the newest two of many\n";
            return 1;
        }
        // Asked for, the rest show too.
        QTest::qWait(300); // the flyout's slide in
        click(find(list, "groupToggle"));
        if (!QTest::qWaitFor([&] { return rows().size() == daemon->history()->count(); })) {
            std::cerr << "expanding an application did not list all its notifications\n";
            return 1;
        }
        // Too many for the room the calendar leaves on this short output: the calendar gives up
        // its time and date to them, and where the list still goes on past the card's edge, that
        // edge fades out.
        auto *flyoutCalendar = find(view.rootObject(), "calendar");
        auto *calendarTime = find(view.rootObject(), "calendarTime");
        auto *fadeBottom = find(view.rootObject(), "notificationFadeBottom");
        if (!QTest::qWaitFor([&] {
                return flyoutCalendar->property("compact").toBool() && !calendarTime->isVisible() &&
                       fadeBottom->isVisible() == !list->property("atYEnd").toBool();
            }))
            return fail("the calendar did not make room for the notifications, or the list's edge does not fade where it is cut off");
        // An action's button runs it, from the history as from a card.
        invoked.clear();
        auto *historyAction = find(list, "notificationHistoryAction");
        if (!historyAction || historyAction->property("text").toString() != "Yes")
            return fail("the history has no button for a notification's action");
        click(historyAction);
        if (!QTest::qWaitFor([&] { return invoked.count() == 1; }) ||
            invoked.at(0).at(1).toString() != "yes") {
            std::cerr << "the history's action button did not run the action\n";
            return 1;
        }
        // The pointer over a notification shows its cross, which removes it.
        const int kept = daemon->history()->count();
        QTest::mouseMove(popover, centre(rows().first()));
        auto *remove = find(rows().first(), "removeNotification");
        if (!remove || !QTest::qWaitFor([&] { return remove->isVisible(); }))
            return fail("the pointer over a notification did not show its cross");
        click(remove);
        if (!QTest::qWaitFor([&] { return daemon->history()->count() == kept - 1 && rows().size() == kept - 1; })) {
            std::cerr << "the cross did not remove the notification\n";
            return 1;
        }
        auto *dndSwitch = find(view.rootObject(), "dndSwitch");
        QTest::qWait(300);
        QTest::mouseMove(dndSwitch->window(), centre(dndSwitch));
        QTest::qWait(50);
        click(dndSwitch, Qt::LeftButton, 80);
        if (!QTest::qWaitFor([&] { return daemon->dnd(); })) {
            std::cerr << "the do-not-disturb switch did nothing\n";
            return 1;
        }
        daemon->setDnd(false);
        click(find(view.rootObject(), "clearNotifications"));
        auto *empty = find(view.rootObject(), "notificationsEmpty");
        if (!QTest::qWaitFor([&] { return daemon->history()->count() == 0 && empty && empty->isVisible(); })) {
            std::cerr << "Clear all did not empty the history and say so\n";
            return 1;
        }
        if (!QTest::qWaitFor([&] { return !flyoutCalendar->property("compact").toBool() && calendarTime->isVisible(); }))
            return fail("the calendar did not show its time and date again once the notifications were gone");
        click(bell); // close the popup
        // The compositor's notification_history action (Super + N) opens the flyout on the output
        // it names, and closes it again.
        subscriber->write(("notifications " + output + "\n").toUtf8());
        if (!QTest::qWaitFor([&] { return inPopover(history); })) {
            std::cerr << "notification_history did not open the clock flyout\n";
            return 1;
        }
        subscriber->write("notifications ELSEWHERE-1\n");
        subscriber->write(("notifications " + output + "\n").toUtf8());
        if (!QTest::qWaitFor([&] { return !history->isVisible() && !popover->isVisible(); })) {
            std::cerr << "notification_history did not close the clock flyout\n";
            return 1;
        }
        // Right-clicking the bell toggles do-not-disturb.
        click(bell, Qt::RightButton);
        if (!QTest::qWaitFor([&] { return daemon->dnd(); })) {
            std::cerr << "right-clicking the bell did not turn do-not-disturb on\n";
            return 1;
        }
        daemon->setDnd(false);
        // So does right-clicking the clock, which shows a crossed-out bell while it is on.
        auto *clockDnd = find(view.rootObject(), "clockDnd");
        click(find(view.rootObject(), "clockButton"), Qt::RightButton);
        if (!QTest::qWaitFor([&] { return daemon->dnd() && clockDnd && clockDnd->isVisible(); })) {
            std::cerr << "right-clicking the clock did not turn do-not-disturb on\n";
            return 1;
        }
        click(find(view.rootObject(), "clockButton"), Qt::RightButton);
        if (!QTest::qWaitFor([&] { return !daemon->dnd() && !clockDnd->isVisible(); })) {
            std::cerr << "right-clicking the clock again did not turn do-not-disturb off\n";
            return 1;
        }
        if (!rewrite(lua))
            return fail("could not restore the configuration");
        controller.reload();
        if (!QTest::qWaitFor([&] { return !bell->isVisible(); })) {
            std::cerr << "the bell stayed once the setting was gone\n";
            return 1;
        }
    }
    // Quick Settings: with the widgets placed in it, a button left of the clock shows their state,
    // and its flyout holds a tile for each beside night light; the bar keeps none of their own.
    {
        // By default, but for the wallpapers.
        const QString quickLua = QString(lua).replace(barWidgets, "widgets={wallpapers='quick'},");
        if (!rewrite(quickLua))
            return fail("could not rewrite the configuration");
        controller.reload();
        // A battery and a link that is down, as the battery test left the fake sysfs.
        SystemStatus fake(screens.filePath("sys"));
        QQmlEngine::setObjectOwnership(&fake, QQmlEngine::CppOwnership);
        view.rootObject()->setProperty("statusSource", QVariant::fromValue(&fake));
        auto *button = find(view.rootObject(), "quickSettingsButton");
        if (!button || !QTest::qWaitFor([&] { return button->isVisible() && button->x() > 0; }))
            return fail("the Quick Settings button did not appear with widgets placed in it");
        for (const char *name : {"networkWidget", "batteryWidget", "audioWidget", "tilingToggle",
                                 "profilesButton", "wallpapersButton", "notificationBell"})
            if (find(view.rootObject(), name)->isVisible()) {
                std::cerr << name << " stayed on the bar while placed in Quick Settings\n";
                return 1;
            }
        click(button);
        auto *quick = find(view.rootObject(), "quickSettings");
        if (!quick || !QTest::qWaitFor([&] { return inPopover(quick); }))
            return fail("clicking the Quick Settings button did not open its flyout");
        auto tile = [&](const QString &name) { return find(quick, "quickTile:" + name); };
        for (const char *name : {"dnd", "nightLight", "tiling", "profiles", "wallpapers", "network"})
            if (!tile(name) || !tile(name)->isVisible()) {
                std::cerr << "Quick Settings has no " << name << " tile\n";
                return 1;
            }
        if (tile("network")->property("label").toString() != "Disconnected" ||
            tile("network")->property("detail").toString() != "wlan0" ||
            !find(quick, "quickBattery")->isVisible())
            return fail("Quick Settings does not show the network and the battery as they are");
        // Do not disturb, night light (through the compositor) and this monitor's tiling.
        auto *daemon = controller.notifications();
        click(tile("dnd"));
        if (!QTest::qWaitFor([&] { return daemon->dnd() && tile("dnd")->property("checked").toBool(); }))
            return fail("the do-not-disturb tile did not turn it on");
        click(tile("dnd"));
        if (!QTest::qWaitFor([&] { return !daemon->dnd(); }))
            return fail("the do-not-disturb tile did not turn it off");
        requests.clear();
        click(tile("nightLight"));
        if (!QTest::qWaitFor([&] { return tile("nightLight")->property("checked").toBool(); }) ||
            requests != QStringList{"night_light_toggle"})
            return fail("the night light tile did not ask the compositor to toggle it");
        click(tile("nightLight"));
        if (!QTest::qWaitFor([&] { return !tile("nightLight")->property("checked").toBool(); }))
            return fail("the night light tile did not toggle it back");
        const bool tiled = panelTiling();
        click(tile("tiling"));
        if (!QTest::qWaitFor([&] { return panelTiling() != tiled && tile("tiling")->property("checked").toBool() != tiled; }))
            return fail("the tiling tile did not toggle this monitor's tiling");
        click(tile("tiling"));
        if (!QTest::qWaitFor([&] { return panelTiling() == tiled; }))
            return fail("the tiling tile did not toggle tiling back");
        // The appearance tile lists the profiles under it; picking one switches to it.
        click(tile("profiles"));
        auto *profiles = find(quick, "quickProfiles");
        if (!profiles || !QTest::qWaitFor([&] { return profiles->isVisible() && profiles->height() > 0; }))
            return fail("the appearance tile did not list the profiles");
        requests.clear();
        QQuickItem *light = nullptr;
        for (auto *row : profiles->childItems())
            if (row->property("text").toString() == "light")
                light = row;
        if (!light)
            return fail("the light profile is not listed in Quick Settings");
        click(light);
        if (!QTest::qWaitFor([&] { return controller.profile() == "light"; }) ||
            requests != QStringList{"profile light"})
            return fail("picking a profile in Quick Settings did not switch to it");
        controller.pickProfile("dark");
        if (!QTest::qWaitFor([&] { return controller.profile() == "dark"; }))
            return fail("the dark profile did not come back");
        // The volume: its slider and mute, the outputs to pick from, and the applications'.
        auto *sound = find(quick, "quickSound");
        auto *volumeSlider = find(quick, "quickVolumeSlider");
        if (!sound || !sound->isVisible() || !volumeSlider)
            return fail("Quick Settings has no volume");
        audio.requests.clear();
        const auto volumeTrack =
            volumeSlider->mapRectToScene(QRectF(0, 0, volumeSlider->width(), volumeSlider->height()));
        QTest::mouseClick(popover, Qt::LeftButton, Qt::NoModifier,
                          QPointF(volumeTrack.left() + volumeTrack.width() * 0.25, volumeTrack.center().y()).toPoint());
        if (audio.requests.size() != 1 || !audio.requests[0].startsWith("volume headset ") ||
            std::abs(audio.requests[0].section(' ', 2).toInt() - 25) > 6) {
            std::cerr << "the Quick Settings volume slider did not set the volume: "
                      << audio.requests.join(", ").toStdString() << '\n';
            return 1;
        }
        click(find(quick, "quickMute"));
        if (!QTest::qWaitFor([&] { return audio.muted(); }))
            return fail("the Quick Settings mute button did not mute");
        click(find(quick, "quickMute"));
        auto *outputsList = find(quick, "quickOutputs");
        click(find(quick, "quickOutputsToggle"));
        if (!outputsList || !QTest::qWaitFor([&] { return outputsList->isVisible() && outputsList->height() > 0; }))
            return fail("the outputs did not open under the volume");
        audio.requests.clear();
        click(findNamed(outputsList, "quickOutputItem", "Speakers"));
        if (!QTest::qWaitFor([&] { return audio.output() == "speakers"; }) ||
            audio.requests != QStringList{"output speakers 2"}) {
            std::cerr << "picking an output in Quick Settings did not switch to it: "
                      << audio.requests.join(", ").toStdString() << '\n';
            return 1;
        }
        auto *streamsList = find(quick, "quickStreams");
        click(find(quick, "quickMixerToggle"));
        if (!streamsList || !QTest::qWaitFor([&] { return streamsList->isVisible() && !outputsList->isVisible(); }))
            return fail("the applications' volumes did not open in place of the outputs");
        QQuickItem *quickStream = nullptr;
        if (!QTest::qWaitFor([&] { return (quickStream = findNamed(streamsList, "quickStreamSlider", {})); }))
            return fail("no application's slider in Quick Settings");
        QTest::qWait(50); // laid out
        audio.requests.clear();
        const auto streamTrack =
            quickStream->mapRectToScene(QRectF(0, 0, quickStream->width(), quickStream->height()));
        QTest::mouseClick(popover, Qt::LeftButton, Qt::NoModifier,
                          QPointF(streamTrack.left() + streamTrack.width() * 0.5, streamTrack.center().y()).toPoint());
        if (audio.requests.size() != 1 || !audio.requests[0].startsWith("stream 41 ")) {
            std::cerr << "the Quick Settings application slider did not set its volume: "
                      << audio.requests.join(", ").toStdString() << '\n';
            return 1;
        }
        // The screen's brightness, only where there is a backlight; the slider sets it (in a
        // fake sysfs tree, by writing the level).
        auto *brightness = find(quick, "quickBrightness");
        if (!brightness || brightness->isVisible())
            return fail("Quick Settings shows a brightness without a backlight");
        QDir lightSys(screens.filePath("light"));
        lightSys.mkpath("class/backlight/fake");
        for (const auto &[name, text] : {std::pair{"max_brightness", "200\n"}, {"brightness", "100\n"}}) {
            QFile level(lightSys.filePath(QString("class/backlight/fake/") + name));
            if (!level.open(QIODevice::WriteOnly) || level.write(text) < 0)
                return fail("could not write the fake backlight");
        }
        Backlight fakeLight(lightSys.path());
        QQmlEngine::setObjectOwnership(&fakeLight, QQmlEngine::CppOwnership);
        view.rootObject()->setProperty("backlightSource", QVariant::fromValue(&fakeLight));
        auto *brightnessSlider = find(quick, "quickBrightnessSlider");
        if (!QTest::qWaitFor([&] { return brightness->isVisible() && brightnessSlider->property("value").toInt() == 50; }))
            return fail("Quick Settings does not show the backlight's level");
        QTest::qWait(50); // laid out
        const auto lightTrack =
            brightnessSlider->mapRectToScene(QRectF(0, 0, brightnessSlider->width(), brightnessSlider->height()));
        QTest::mouseClick(popover, Qt::LeftButton, Qt::NoModifier,
                          QPointF(lightTrack.left() + lightTrack.width() * 0.8, lightTrack.center().y()).toPoint());
        QFile written(lightSys.filePath("class/backlight/fake/brightness"));
        if (!written.open(QIODevice::ReadOnly) || std::abs(written.readAll().trimmed().toInt() - 160) > 12 ||
            std::abs(fakeLight.percent() - 80) > 6) {
            std::cerr << "the brightness slider did not set the backlight: " << fakeLight.percent() << "%\n";
            return 1;
        }
        view.rootObject()->setProperty("backlightSource", QVariant::fromValue(controller.backlight()));
        // The button's wheel changes the volume and a middle click mutes, as on the bar's.
        audio.requests.clear();
        const int before = audio.volume();
        QWheelEvent notch(centre(button), view.mapToGlobal(centre(button)), {}, {0, 120},
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(&view, &notch);
        if (audio.volume() != before + 5)
            return fail("the wheel on the Quick Settings button did not raise the volume");
        click(button, Qt::MiddleButton);
        if (!QTest::qWaitFor([&] { return audio.muted(); }))
            return fail("a middle click on the Quick Settings button did not mute");
        audio.toggleMute();
        // The wallpaper tile opens the bar's own picker in its place.
        if (!quick->isVisible())
            click(button);
        if (!QTest::qWaitFor([&] { return inPopover(quick); }))
            return fail("Quick Settings did not open again");
        click(tile("wallpapers"));
        auto *picker = find(view.rootObject(), "wallpaperPicker");
        if (!QTest::qWaitFor([&] { return picker && inPopover(picker) && !quick->isVisible(); }))
            return fail("the wallpaper tile did not open the wallpaper picker");
        QTest::keyClick(picker->window(), Qt::Key_Escape);
        if (!QTest::qWaitFor([&] { return !popover->isVisible(); }))
            return fail("the wallpaper picker did not close");
        view.rootObject()->setProperty("statusSource", QVariant::fromValue(controller.status()));
        // With nothing placed in it (do-not-disturb, which this test's configuration leaves in
        // it, switched off), the button goes.
        if (!rewrite(QString(lua).replace("widgets={", "widgets={notifications=false,")))
            return fail("could not rewrite the configuration");
        controller.reload();
        if (!QTest::qWaitFor([&] { return !button->isVisible(); }))
            return fail("the Quick Settings button stayed with nothing placed in it");
        if (!rewrite(lua))
            return fail("could not restore the configuration");
        controller.reload();
    }
    // The system tray: hidden while empty, a button for each item shown in the order they came,
    // and clicks and the wheel passed on to the item's application. The items are put in the
    // model by hand, as the D-Bus host puts them.
    {
        auto *trayModel = controller.tray();
        auto *trayRow = find(view.rootObject(), "tray");
        if (!trayRow || trayRow->isVisible()) {
            std::cerr << "the tray shows without items\n";
            return 1;
        }
        auto solid = [](const QColor &color) {
            QImage image(22, 22, QImage::Format_ARGB32);
            image.fill(color);
            return image;
        };
        auto trayItem = [&](const QString &key, const QString &status, const QColor &color) {
            TrayItem item;
            item.key = key;
            item.id = key;
            item.title = "Title of " + key;
            item.status = status;
            item.icon = {solid(color)};
            return item;
        };
        TrayItem first = trayItem("first", "Active", Qt::green);
        first.toolTipTitle = "First";
        first.toolTipText = "<b>not markup</b>";
        trayModel->add(first);
        trayModel->add(trayItem("asleep", "Passive", Qt::red));
        trayModel->add(trayItem("third", "NeedsAttention", Qt::blue));
        auto trayButtons = [&] {
            QList<QQuickItem *> shown;
            for (auto *child : trayRow->childItems())
                if (child->objectName() == "trayItem" && child->isVisible())
                    shown << child;
            std::sort(shown.begin(), shown.end(), [](QQuickItem *a, QQuickItem *b) { return a->x() < b->x(); });
            return shown;
        };
        if (!QTest::qWaitFor([&] { return trayRow->isVisible() && trayButtons().size() == 2; }) ||
            trayButtons()[0]->property("key").toString() != "first" ||
            trayButtons()[1]->property("key").toString() != "third" ||
            trayButtons()[0]->property("toolTip").toString() != "First\n<b>not markup</b>") {
            std::cerr << "the tray does not show its items in order, leaving out the passive one\n";
            return 1;
        }
        // The icon comes from the model's pixels.
        auto iconColor = [&](QQuickItem *button) {
            const QImage frame = view.grabWindow();
            return frame.pixelColor(centre(button));
        };
        if (!QTest::qWaitFor([&] { return iconColor(trayButtons()[0]) == QColor(Qt::green); }) ||
            iconColor(trayButtons()[1]) != QColor(Qt::blue)) {
            std::cerr << "the tray does not draw its items' icons\n";
            return 1;
        }
        QSignalSpy activated(trayModel, &TrayModel::activateRequested);
        QSignalSpy secondary(trayModel, &TrayModel::secondaryActivateRequested);
        QSignalSpy context(trayModel, &TrayModel::contextMenuRequested);
        QSignalSpy scrolled(trayModel, &TrayModel::scrollRequested);
        QQuickItem *button = trayButtons()[0];
        // The point is on the screen: this panel is at the bottom of a 720 pixel high output.
        const QPoint at = centre(button);
        const QVariantList point{"first", at.x(), 720 - view.height() + at.y()};
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, at);
        if (!QTest::qWaitFor([&] { return activated.size() == 1; }) || activated[0] != point) {
            std::cerr << "a left click on a tray item did not activate it\n";
            return 1;
        }
        QTest::mouseClick(&view, Qt::MiddleButton, Qt::NoModifier, at);
        if (!QTest::qWaitFor([&] { return secondary.size() == 1; }) || secondary[0] != point) {
            std::cerr << "a middle click on a tray item did not reach its secondary action\n";
            return 1;
        }
        QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, at);
        if (!QTest::qWaitFor([&] { return context.size() == 1; }) || context[0] != point) {
            std::cerr << "a right click on a tray item without a menu did not ask for its own\n";
            return 1;
        }
        // The wheel goes to the item a notch at a time, not to the workspaces.
        switches.clear();
        scrollAt(at, -120);
        scrollAt(at, 60);
        scrollAt(at, 60);
        QWheelEvent sideways(at, view.mapToGlobal(at), QPoint(), QPoint(120, 0), Qt::NoButton, Qt::NoModifier,
                             Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&view, &sideways);
        if (!QTest::qWaitFor([&] { return scrolled.size() == 3; }) ||
            scrolled[0] != QVariantList{"first", -120, "vertical"} ||
            scrolled[1] != QVariantList{"first", 120, "vertical"} ||
            scrolled[2] != QVariantList{"first", 120, "horizontal"} || !switches.isEmpty()) {
            std::cerr << "the wheel over a tray item did not scroll it\n";
            return 1;
        }
        // A changed picture is drawn again; an item going passive leaves.
        trayModel->find("first")->icon = {solid(Qt::yellow)};
        trayModel->changed("first", true);
        if (!QTest::qWaitFor([&] { return iconColor(trayButtons()[0]) == QColor(Qt::yellow); })) {
            std::cerr << "the tray did not draw an item's new icon\n";
            return 1;
        }
        trayModel->find("first")->status = "Passive";
        trayModel->changed("first", true);
        if (!QTest::qWaitFor([&] { return trayButtons().size() == 1; }) ||
            trayButtons()[0]->property("key").toString() != "third") {
            std::cerr << "a passive tray item stayed\n";
            return 1;
        }
        // An item's menu opens on a right press, above the item in the popover; a submenu opens
        // beside its entry, the menu staying, and picking an entry closes it. The application
        // hears of each level as it opens and closes.
        auto menuEntry = [](QVariantMap properties, std::vector<int> children = {}) {
            TrayMenuEntry entry;
            entry.properties = std::move(properties);
            entry.children = std::move(children);
            entry.read();
            return entry;
        };
        TrayItem withMenu = trayItem("menu", "Active", Qt::cyan);
        withMenu.menuPath = "/MenuBar";
        withMenu.menu = {{0, menuEntry({}, {1, 2, 3, 4})},
                         {1, menuEntry({{"label", "_Open"}})},
                         {2, menuEntry({{"type", "separator"}})},
                         {3, menuEntry({{"label", "_More"}}, {31, 32})},
                         {31, menuEntry({{"label", "_Deep"}, {"toggle-type", "checkmark"}, {"toggle-state", 1}})},
                         {32, menuEntry({{"label", "Radio"}, {"toggle-type", "radio"}, {"toggle-state", 0}})},
                         {4, menuEntry({{"label", "Disabled"}, {"enabled", false}})}};
        trayModel->add(withMenu);
        auto trayButton = [&](const QString &key) -> QQuickItem * {
            for (auto *shown : trayButtons())
                if (shown->property("key").toString() == key)
                    return shown;
            return nullptr;
        };
        // Laid out after the one already there.
        if (!QTest::qWaitFor([&] {
                const auto shown = trayButtons();
                return shown.size() == 2 && shown[0]->property("key").toString() == "third" &&
                       shown[1]->property("key").toString() == "menu";
            })) {
            std::cerr << "a tray item with a menu did not show after the others\n";
            return 1;
        }
        QSignalSpy opened(trayModel, &TrayModel::menuOpenRequested);
        QSignalSpy closed(trayModel, &TrayModel::menuCloseRequested);
        QSignalSpy picked(trayModel, &TrayModel::menuClickRequested);
        auto *trayMenu = find(view.rootObject(), "trayMenu");
        auto trayEntries = [&] {
            QList<QQuickItem *> found;
            std::function<void(QQuickItem *)> walk = [&](QQuickItem *item) {
                for (auto *child : item->childItems()) {
                    if (child->objectName() == "trayMenuItem" && child->isVisible())
                        found << child;
                    walk(child);
                }
            };
            if (trayMenu)
                walk(trayMenu);
            std::sort(found.begin(), found.end(), [](QQuickItem *a, QQuickItem *b) {
                return a->mapToScene({0, 0}).y() < b->mapToScene({0, 0}).y();
            });
            return found;
        };
        auto trayLabels = [&] {
            QStringList labels;
            for (auto *entry : trayEntries())
                labels << entry->property("text").toString();
            return labels;
        };
        auto trayEntry = [&](const QString &label) -> QQuickItem * {
            for (auto *entry : trayEntries())
                if (entry->property("text").toString() == label)
                    return entry;
            return nullptr;
        };
        auto trayMenuShown = [&] {
            return inPopover(trayMenu);
        };
        auto menuOpen = [&] { return view.rootObject()->property("menuOpen").toBool(); };
        const QPoint menuAt = centre(trayButton("menu"));
        // Held past the long-press time, as with the bar's menus.
        QTest::mousePress(&view, Qt::RightButton, Qt::NoModifier, menuAt);
        QTest::qWait(1000);
        QTest::mouseRelease(&view, Qt::RightButton, Qt::NoModifier, menuAt);
        if (!trayMenu || !QTest::qWaitFor([&] {
                return trayMenuShown() && trayLabels() == QStringList{"Open", "More", "Disabled"} &&
                       opened.size() == 1;
            }) ||
            opened[0] != QVariantList{"menu", 0} || context.size() != 1) {
            std::cerr << "right-clicking a tray item did not open its menu: "
                      << trayLabels().join("|").toStdString() << '\n';
            return 1;
        }
        click(trayEntry("Disabled"));
        QTest::qWait(100);
        if (!picked.isEmpty() || !trayMenuShown()) {
            std::cerr << "a disabled tray menu entry could be picked\n";
            return 1;
        }
        click(trayEntry("More"));
        if (!QTest::qWaitFor([&] {
                return trayEntry("Deep") && trayEntry("Radio") && trayEntry("Open") &&
                       trayEntry("More")->property("expanded").toBool() && opened.size() == 2;
            }) ||
            opened[1] != QVariantList{"menu", 3} || !closed.isEmpty() ||
            !trayEntry("Deep")->property("modelData").toMap()["checked"].toBool() ||
            trayEntry("Radio")->property("modelData").toMap()["checked"].toBool()) {
            std::cerr << "a tray submenu did not open beside its entry: " << trayLabels().join("|").toStdString() << '\n';
            return 1;
        }
        QTest::keyClick(popover, Qt::Key_Left);
        if (!QTest::qWaitFor([&] { return !trayEntry("Deep") && closed.size() == 1; }) ||
            closed[0] != QVariantList{"menu", 3} || !trayMenuShown() ||
            trayLabels() != QStringList{"Open", "More", "Disabled"}) {
            std::cerr << "closing a tray submenu did not tell its application, or closed the menu\n";
            return 1;
        }
        // The application changing the menu while it is open shows at once.
        auto &openEntry = trayModel->find("menu")->menu[1];
        openEntry.properties["label"] = "_Reopen";
        openEntry.read();
        trayModel->menuEdited("menu");
        if (!QTest::qWaitFor([&] { return trayEntry("Reopen") != nullptr; })) {
            std::cerr << "a tray menu did not follow its application's change\n";
            return 1;
        }
        // Picked with a submenu open, both levels close, the deeper first.
        click(trayEntry("More"));
        if (!QTest::qWaitFor([&] { return trayEntry("Deep") && opened.size() == 3; }))
            return fail("the tray submenu did not open again");
        click(trayEntry("Reopen"));
        if (!QTest::qWaitFor([&] { return picked.size() == 1 && !menuOpen(); }) ||
            picked[0] != QVariantList{"menu", 1} ||
            !QTest::qWaitFor([&] { return !popover->isVisible() && closed.size() == 3; }) ||
            closed[1] != QVariantList{"menu", 3} || closed[2] != QVariantList{"menu", 0}) {
            std::cerr << "picking a tray menu entry did not reach the item and close the menu\n";
            return 1;
        }
        // An item that is only a menu opens it on a left click; a second press closes it.
        trayModel->find("menu")->itemIsMenu = true;
        trayModel->changed("menu", false);
        QTest::qWait(50);
        const qsizetype activations = activated.size();
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, menuAt);
        if (!QTest::qWaitFor([&] { return trayMenuShown(); }) || activated.size() != activations) {
            std::cerr << "a left click on a tray item that is only a menu did not open it\n";
            return 1;
        }
        // A second right click closes it.
        click(trayButton("menu"), Qt::RightButton);
        if (!QTest::qWaitFor([&] { return !menuOpen(); })) {
            std::cerr << "a second right click did not close the tray menu\n";
            return 1;
        }
        // Escape closes it too.
        click(trayButton("menu"));
        if (!QTest::qWaitFor([&] { return trayMenuShown(); }))
            return fail("the tray menu did not open again");
        QTest::keyClick(trayMenu->window(), Qt::Key_Escape);
        if (!QTest::qWaitFor([&] { return !menuOpen(); })) {
            std::cerr << "Escape did not close the tray menu\n";
            return 1;
        }
        // An item that cannot be activated shows its menu after a left click.
        trayModel->find("menu")->itemIsMenu = false;
        trayModel->changed("menu", false);
        QTest::qWait(50);
        QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, menuAt);
        if (!QTest::qWaitFor([&] { return activated.size() == activations + 1; }))
            return fail("clicking the tray menu's item did not activate it");
        Q_EMIT trayModel->activationRefused("menu");
        if (!QTest::qWaitFor([&] { return trayMenuShown(); })) {
            std::cerr << "a tray item that refused activation did not show its menu\n";
            return 1;
        }
        // The menu goes with its item.
        trayModel->remove("menu");
        if (!QTest::qWaitFor([&] { return !menuOpen() && !popover->isVisible(); })) {
            std::cerr << "a tray menu stayed open after its item went\n";
            return 1;
        }
        trayModel->clear();
        if (!QTest::qWaitFor([&] { return !trayRow->isVisible(); })) {
            std::cerr << "the tray stayed with no items\n";
            return 1;
        }
    }
    // The design tokens follow the configuration: the animation settings set every duration,
    // popups are opaque over a translucent bar, and a light panel is known for one.
    {
        QQmlComponent probe(view.engine());
        // In the module's folder, where Theme is found as the panel finds it.
        probe.setData("import QtQuick\nQtObject { property QtObject theme: Theme }",
                      QUrl("qrc:/shell/ShaodeskShell/ThemeProbe.qml"));
        std::unique_ptr<QObject> holder(probe.create());
        auto *theme = holder ? holder->property("theme").value<QObject *>() : nullptr;
        if (!theme) {
            std::cerr << "Theme did not load: " << probe.errorString().toStdString() << '\n';
            return 1;
        }
        auto token =[&](const char *name) { return theme->property(name); };
        if (token("durationFast").toInt() != 120 || token("light").toBool() ||
            token("surface").value<QColor>() != controller.panelColor())
            return fail("Theme does not follow the default configuration");
        if (!rewrite(QString(lua).replace("shell={wallpaper", "animations={speed=2},shell={wallpaper")))
            return fail("could not rewrite the configuration");
        controller.reload();
        if (!QTest::qWaitFor([&] { return token("durationFast").toInt() == 60; }))
            return fail("Theme's durations do not follow animations.speed");
        if (!rewrite(QString(lua).replace(
                "shell={wallpaper", "animations={enabled=false},shell={panel_color='#f3f2fbcc',"
                                    "text_color='#141a48',accent='#4a64dc',wallpaper")))
            return fail("could not rewrite the configuration");
        controller.reload();
        if (!QTest::qWaitFor([&] { return token("light").toBool(); }) ||
            token("durationFast").toInt() != 0 || token("durationSlow").toInt() != 0 ||
            token("bar").value<QColor>().alpha() != 0xcc ||
            token("surface").value<QColor>().alpha() != 255 ||
            token("hover").value<QColor>().alpha() == 0 ||
            token("textOnAccent").value<QColor>() != token("surface").value<QColor>()) {
            std::cerr << "Theme does not follow a light, translucent profile without animations\n";
            return 1;
        }
        if (!rewrite(lua))
            return fail("could not restore the configuration");
        controller.reload();
    }
    std::cout << "Hover/click, launcher keyboard focus, search, command launch, tiling toggle, and "
                 "workspace indicator, task and bar context menus, pinning into a window's slot, "
                 "reordering pins, "
                 "task reordering, grouped windows, the volume control, the command palette, and the "
                 "notification bell, cards and history, the tray, and the design tokens passed\n";
}
