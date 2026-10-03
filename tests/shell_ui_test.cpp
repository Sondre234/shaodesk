// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.hpp"
#include "controller.hpp"
#include "view.hpp"
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJSValue>
#include <QLocalServer>
#include <QLocalSocket>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
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
int main(int argc, char **argv) {
    // A named screen, as compositor outputs are: the workspace indicator is keyed by it.
    QTemporaryDir screens;
    QFile layout(screens.filePath("screens.json"));
    if (!screens.isValid() || !layout.open(QIODevice::WriteOnly) ||
        layout.write(R"({"screens": [{"name": "TEST-1", "x": 0, "y": 0, "width": 1280,
                         "height": 720, "logicalDpi": 96, "logicalBaseDpi": 96, "dpr": 1}]})") < 0)
        return 1;
    layout.close();
    qputenv("QT_QPA_PLATFORM", ("offscreen:configfile=" + layout.fileName()).toLocal8Bit());
    // One installed application, found by its StartupWMClass, and a private pin store. GLib
    // caches these directories on first use, so they are set before anything starts.
    QDir(screens.path()).mkpath("data/applications");
    QFile desktopFile(screens.filePath("data/applications/shaode-test-app.desktop"));
    if (!desktopFile.open(QIODevice::WriteOnly) ||
        desktopFile.write("[Desktop Entry]\nType=Application\nName=Fake app\nExec=true\n"
                          "StartupWMClass=Fake\n") < 0)
        return 1;
    desktopFile.close();
    QFile otherFile(screens.filePath("data/applications/shaode-test-other.desktop"));
    if (!otherFile.open(QIODevice::WriteOnly) ||
        otherFile.write("[Desktop Entry]\nType=Application\nName=Other app\nExec=true\n") < 0)
        return 1;
    otherFile.close();
    qputenv("XDG_DATA_HOME", screens.filePath("data").toLocal8Bit());
    qputenv("XDG_DATA_DIRS", screens.filePath("none").toLocal8Bit());
    qputenv("XDG_STATE_HOME", screens.filePath("state").toLocal8Bit());
    const auto pins = screens.filePath("state/shaode/pinned");
    QGuiApplication app(argc, argv);
    if (argc != 2)
        return 1;
    QTemporaryDir directory;
    if (!directory.isValid())
        return 1;
    auto config = directory.filePath("init.lua");
    auto marker = directory.filePath("launched");
    QFile file(config);
    if (!file.open(QIODevice::WriteOnly))
        return 1;
    // Long Lua strings preserve paths without shell interpolation.
    file.write(
        (QString(
             "return {shell={launchers={{name='Test app',command={[[%1]],'-E','touch',[[%2]]}}}}}")
             .arg(QString::fromLocal8Bit(argv[1]), marker))
            .toUtf8());
    file.close();
    // A stand-in for the compositor's control socket, with this screen as its only output.
    // Tiling is per output; the focused one it reports first is always the opposite of this
    // screen's, as if another monitor had focus, so the panel must show its own.
    const auto output = app.primaryScreen()->name();
    auto state = [&output](bool tiling, int workspace) {
        return QString("tiling %1\nworkspace %2\noutput %3 %2 1,2 %4\n")
            .arg(tiling ? "off" : "on")
            .arg(workspace)
            .arg(output)
            .arg(tiling ? "on" : "off")
            .toUtf8();
    };
    QLocalServer compositor;
    QLocalSocket *subscriber = nullptr;
    bool toggled = false;
    int currentWorkspace = 2;
    QStringList switches;
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
            } else if (request.startsWith("output ")) {
                switches.push_back(QString::fromUtf8(request).trimmed());
                client->write("ok\n");
                client->disconnectFromServer();
                currentWorkspace = request.trimmed().split(' ').last().toInt();
                subscriber->write(state(toggled, currentWorkspace));
            }
        });
    });
    if (!compositor.listen(directory.filePath("control.sock")))
        return 1;
    qputenv("SHAODE_SOCKET", compositor.fullServerName().toLocal8Bit());
    ShellController controller(config.toStdString());
    ShellView view(controller, app.primaryScreen(), false, true);
    if (view.status() != QQuickView::Ready)
        return 1;
    view.show();
    if (!QTest::qWaitForWindowExposed(&view))
        return 1;
    const QPoint start(30, controller.panelHeight() / 2);
    QTest::mouseMove(&view, start);
    QTest::qWait(200); // Hover first: a tooltip must not swallow the following press.
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, start);
    if (!QTest::qWaitFor([&] { return view.rootObject()->property("launcherOpen").toBool(); })) {
        std::cerr << "hover then click did not open the launcher\n";
        return 1;
    }
    auto *search = view.rootObject()->findChild<QQuickItem *>("applicationSearch");
    if (!search || !QTest::qWaitFor([&] { return search->hasActiveFocus(); })) {
        std::cerr << "launcher search did not receive keyboard focus\n";
        return 1;
    }
    for (Qt::Key key : {Qt::Key_T, Qt::Key_E, Qt::Key_S, Qt::Key_T})
        QTest::keyClick(&view, key);
    QTest::keyClick(&view, Qt::Key_Return);
    if (!QTest::qWaitFor([&] { return QFile::exists(marker); })) {
        std::cerr << "search and Enter did not launch the configured command\n";
        return 1;
    }
    if (view.rootObject()->property("launcherOpen").toBool()) {
        std::cerr << "launcher remained open after launching\n";
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
    // Repeater items are found through the item tree rather than as QObject children.
    std::function<QQuickItem *(QQuickItem *, const QString &)> find =
        [&find](QQuickItem *item, const QString &name) -> QQuickItem * {
        if (item->objectName() == name)
            return item;
        for (auto *child : item->childItems())
            if (auto *found = find(child, name))
                return found;
        return nullptr;
    };
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
    auto centre = [&](QQuickItem *item) {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    };
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, centre(workspace(3)));
    if (!QTest::qWaitFor([&] { return workspace(3)->property("current").toBool(); }) ||
        switches != QStringList{"output " + output + " workspace 3"}) {
        std::cerr << "clicking a workspace did not switch to it\n";
        return 1;
    }
    auto scroll = [&](int delta) {
        const QPoint at = centre(workspace(2));
        QWheelEvent event(at, view.mapToGlobal(at), QPoint(), QPoint(0, delta), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&view, &event);
    };
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
    // Context menus: a task's, then the bar's. Stand-in tasks replace the Wayland ones.
    auto *tasks = view.rootObject()->findChild<QQuickItem *>("taskList");
    QQmlComponent fakeTasks(view.engine());
    fakeTasks.setData("import QtQml.Models\nListModel { ListElement { taskId: 7; title: 'Fake'; "
                      "appId: 'fake'; active: false; minimized: false } }",
                      QUrl());
    QObject *fakeModel = fakeTasks.create();
    if (!tasks || !fakeModel)
        return 1;
    QQmlEngine::setObjectOwnership(fakeModel, QQmlEngine::CppOwnership);
    view.rootObject()->setProperty("taskSource", QVariant::fromValue(fakeModel));
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
        return 1;
    auto center = [](QQuickItem *item) {
        return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    };
    auto *menu = view.rootObject()->findChild<QQuickItem *>("contextMenu");
    // Repeater delegates are visual children only, so walk the item tree.
    std::function<QQuickItem *(QQuickItem *, const QString &)> findMenuItem =
        [&](QQuickItem *parent, const QString &text) -> QQuickItem * {
        for (auto *item : parent->childItems()) {
            if (item->objectName() == "contextMenuItem" && item->property("text") == text)
                return item;
            if (auto *found = findMenuItem(item, text))
                return found;
        }
        return nullptr;
    };
    auto menuItem = [&](const QString &text) { return findMenuItem(menu, text); };
    // The whole menu must lie inside the panel surface, which grows to make room for it.
    auto menuShown = [&] {
        QRectF area = menu->mapRectToScene(QRectF(0, 0, menu->width(), menu->height()));
        return menu->isVisible() && view.height() > controller.panelExtent() && area.top() >= 0 &&
               area.bottom() <= view.height();
    };
    const QPoint entry = center(task);
    // Held past the long-press time, which once swallowed the right click.
    QTest::mousePress(&view, Qt::RightButton, Qt::NoModifier, entry);
    QTest::qWait(1000);
    QTest::mouseRelease(&view, Qt::RightButton, Qt::NoModifier, center(task));
    if (!QTest::qWaitFor([&] {
            return view.rootObject()->property("taskMenuId").toInt() == 7 && menuShown();
        }) ||
        !menuItem("Maximize / restore") || !menuItem("Minimize") || !menuItem("Close window")) {
        std::cerr << "right-clicking a task did not show its menu\n";
        return 1;
    }
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, center(menuItem("Minimize")));
    if (!QTest::qWaitFor([&] { return !view.rootObject()->property("menuOpen").toBool(); }) ||
        !QTest::qWaitFor([&] { return view.height() == controller.panelExtent(); })) {
        std::cerr << "choosing a task menu item did not close the menu\n";
        return 1;
    }
    // The task's window belongs to an installed application, which its menu pins. Pinned, the
    // window takes over the application's slot instead of adding a button; with no window left
    // the slot's launcher returns, and its own menu unpins it. Pins are remembered in the state
    // directory.
    auto readPins = [&] {
        QFile file(pins);
        return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
    };
    auto pinned = [&] { return find(view.rootObject(), "pinned:shaode-test-app.desktop"); };
    auto pinnedTask = [&] { return find(view.rootObject(), "pinnedTask:shaode-test-app.desktop"); };
    QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, center(task));
    if (!QTest::qWaitFor([&] { return menuShown() && menuItem("Pin to taskbar"); })) {
        std::cerr << "a task's menu did not offer to pin its application\n";
        return 1;
    }
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, center(menuItem("Pin to taskbar")));
    if (!QTest::qWaitFor([&] { return pinned() != nullptr; }) ||
        !controller.isPinned("shaode-test-app.desktop") ||
        readPins() != "shaode-test-app.desktop\n") {
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
    if (!QTest::qWaitFor([&] { return view.height() == controller.panelExtent(); })) {
        std::cerr << "the panel did not shrink after pinning\n";
        return 1;
    }
    // Dragging a pinned slot's window onto another pinned slot moves the pin there.
    controller.pin("shaode-test-other.desktop");
    auto other = [&] { return find(view.rootObject(), "pinned:shaode-test-other.desktop"); };
    if (!QTest::qWaitFor([&] {
            return other() && other()->isVisible() &&
                   center(other()).x() > center(pinnedTask()).x();
        }))
        return 1;
    {
        const QPoint from = center(pinnedTask()), to = center(other());
        QTest::mousePress(&view, Qt::LeftButton, Qt::NoModifier, from);
        for (int step = 1; step <= 10; ++step) {
            QTest::mouseMove(&view, from + (to - from) * step / 10);
            QTest::qWait(10);
        }
        QTest::mouseRelease(&view, Qt::LeftButton, Qt::NoModifier, to);
    }
    if (!QTest::qWaitFor(
            [&] { return readPins() == "shaode-test-other.desktop\nshaode-test-app.desktop\n"; }) ||
        !QTest::qWaitFor([&] {
            return pinnedTask() && other() &&
                   other()->mapToScene({0, 0}).x() < pinnedTask()->mapToScene({0, 0}).x();
        })) {
        std::cerr << "dragging a pinned window onto another pinned slot did not move its pin\n";
        return 1;
    }
    controller.unpin("shaode-test-other.desktop");
    editTasks("model.remove(0)");
    if (!QTest::qWaitFor([&] { return !pinnedTask() && pinned()->isVisible(); })) {
        std::cerr << "a pinned slot did not show its launcher again once its window closed\n";
        return 1;
    }
    QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, center(pinned()));
    if (!QTest::qWaitFor([&] { return menuShown() && menuItem("Unpin from taskbar"); }) ||
        !menuItem("Open Fake app")) {
        std::cerr << "a pinned application's menu did not offer to unpin it\n";
        return 1;
    }
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier,
                      center(menuItem("Unpin from taskbar")));
    if (!QTest::qWaitFor([&] { return pinned() == nullptr; }) || !readPins().isEmpty() ||
        controller.isPinned("shaode-test-app.desktop")) {
        std::cerr << "unpinning did not remove and forget the taskbar button\n";
        return 1;
    }
    if (!QTest::qWaitFor([&] { return view.height() == controller.panelExtent(); })) {
        std::cerr << "the panel did not shrink after unpinning\n";
        return 1;
    }
    editTasks("model.append({ taskId: 7, title: 'Fake', appId: 'fake', active: false, "
              "minimized: false })");
    if (!(task = listedTask(0)))
        return 1;
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
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, center(menuItem("Turn tiling off")));
    if (!QTest::qWaitFor([&] { return !toggled && !panelTiling(); }) ||
        view.rootObject()->property("menuOpen").toBool()) {
        std::cerr << "the bar menu did not toggle tiling off\n";
        return 1;
    }
    // Dragging a task along the bar moves it, not the whole list, as far as it is dragged.
    // Each belongs to a different application, or they would share a button.
    editTasks("model.append({ taskId: 8, title: 'Second', appId: 'second', active: false, "
              "minimized: false })");
    editTasks("model.append({ taskId: 9, title: 'Third', appId: 'third', active: false, "
              "minimized: false })");
    QQuickItem *third = listedTask(2);
    if (!third)
        return 1;
    auto taskIdAt = [&](int index) {
        QJSValue row;
        QMetaObject::invokeMethod(fakeModel, "get", Q_RETURN_ARG(QJSValue, row),
                                  Q_ARG(int, index));
        return row.property("taskId").toInt();
    };
    const QPoint from = center(task), to = center(third) + QPoint(third->width() / 4, 0);
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
              "minimized: false })");
    editTasks("model.append({ taskId: 11, title: 'Group two', appId: 'grouped', active: true, "
              "minimized: false })");
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
    auto *groupList = view.rootObject()->findChild<QQuickItem *>("groupList");
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
    QTest::mouseMove(&view, center(stack));
    if (!groupList || !QTest::qWaitFor([&] {
            auto box =
                groupList->mapRectToScene(QRectF(0, 0, groupList->width(), groupList->height()));
            return groupList->isVisible() && groupRows() == 2 && box.top() >= 0 &&
                   view.height() > controller.panelExtent() &&
                   !view.rootObject()->property("menuOpen").toBool();
        })) {
        std::cerr << "hovering a stacked task did not list its windows "
                  << stack->property("hovered").toBool()
                  << view.rootObject()->property("groupOpen").toBool() << groupRows()
                  << groupList->isVisible() << view.height() << "\n";
        return 1;
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
    const QPoint row = center(firstRow(groupList));
    for (int step = 1; step <= 5; ++step) {
        QTest::mouseMove(&view, center(stack) + (row - center(stack)) * step / 5);
        QTest::qWait(10);
    }
    QTest::qWait(600);
    if (!groupList->isVisible()) {
        std::cerr << "moving from a stacked task to its windows hid them\n";
        return 1;
    }
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, row);
    if (!QTest::qWaitFor(
            [&] { return !groupList->isVisible() && view.height() == controller.panelExtent(); })) {
        std::cerr << "choosing one of a stacked task's windows did not hide them\n";
        return 1;
    }
    // Hovered again, the list goes once the pointer leaves.
    QTest::mouseMove(&view, center(stack));
    if (!QTest::qWaitFor([&] { return groupList->isVisible(); })) {
        std::cerr << "hovering a stacked task again did not list its windows\n";
        return 1;
    }
    QTest::mouseMove(&view, empty);
    if (!QTest::qWaitFor(
            [&] { return !groupList->isVisible() && view.height() == controller.panelExtent(); })) {
        std::cerr << "leaving a stacked task did not hide its windows\n";
        return 1;
    }
    // Dragging the stack moves all its windows together.
    {
        const QPoint from = center(stack), to = center(listedTask(0)) - QPoint(8, 0);
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
    FakeAudio audio;
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
    auto wheel = [&](int delta) {
        QWheelEvent event(center(volume), view.mapToGlobal(center(volume)), {}, {0, delta},
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
    auto above = [&](QQuickItem *popup) {
        auto box = popup->mapRectToScene(QRectF(0, 0, popup->width(), popup->height()));
        auto widget = volume->mapRectToScene(QRectF(0, 0, volume->width(), volume->height()));
        return popup->isVisible() && box.bottom() <= widget.top() &&
               box.left() <= widget.center().x() && box.right() >= widget.center().x();
    };
    auto *outputs = view.rootObject()->findChild<QQuickItem *>("audioOutputs");
    auto *mixer = view.rootObject()->findChild<QQuickItem *>("audioMixer");
    QTest::mouseClick(&view, Qt::RightButton, Qt::NoModifier, center(volume));
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
        return 1;
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, center(headset));
    if (!QTest::qWaitFor([&] { return !view.rootObject()->property("menuOpen").toBool(); }) ||
        audio.requests != QStringList{"output headset 2"} || audio.output() != "headset") {
        std::cerr << "choosing an output did not switch to it: "
                  << audio.requests.join(", ").toStdString() << '\n';
        return 1;
    }
    audio.requests.clear();
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, center(volume));
    if (!mixer || !QTest::qWaitFor([&] { return above(mixer); })) {
        std::cerr << "clicking the volume control did not show the mixer above it\n";
        return 1;
    }
    QQuickItem *streamSlider = nullptr;
    if (!QTest::qWaitFor(
            [&] { return (streamSlider = findNamed(mixer, "audioStreamSlider", {})); }))
        return 1;
    // The first application's slider, clicked three quarters along.
    const auto track =
        streamSlider->mapRectToScene(QRectF(0, 0, streamSlider->width(), streamSlider->height()));
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier,
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
    QTest::keyClick(&view, Qt::Key_Escape);
    if (!QTest::qWaitFor([&] { return view.height() == controller.panelExtent(); })) {
        std::cerr << "the mixer did not close\n";
        return 1;
    }
    std::cout << "Hover/click, launcher keyboard focus, search, command launch, tiling toggle, and "
                 "workspace indicator, task and bar context menus, pinning into a window's slot, "
                 "reordering pins, "
                 "task reordering, grouped windows, and the volume control passed\n";
}
