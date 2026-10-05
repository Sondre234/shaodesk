// SPDX-License-Identifier: GPL-3.0-or-later
// The system tray on a private session bus: a dbus-daemon this test starts and kills, never the
// real one. Fake items (tests/tray_fake_item.hpp), each on a connection of its own, play the
// applications.
#include "tray_fake_item.hpp"
#include "tray_host.hpp"
#include "tray_watcher.hpp"
#include <QDBusConnectionInterface>
#include <QDir>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <memory>
#include <sys/stat.h>

using namespace faketray;

// How a test application's item behaves.
struct Options {
    bool byPath = false; // registers by object path, as Ayatana's library does
    bool getAll = true;
    bool activate = true;
    bool registers = true;
};

class TrayDbusTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;
    QProcess bus_;
    QString address_;
    int connections_ = 0;

    // A new connection to the private bus, as another process would have.
    QDBusConnection connect() {
        return QDBusConnection::connectToBus(address_, QString("tray-test-%1").arg(++connections_));
    }
    // Closes a connection, as a process quitting would. It only closes once nothing refers to it,
    // so `bus` is cleared first.
    void disconnect(QDBusConnection &bus) {
        const QString name = bus.name();
        bus = QDBusConnection(QString());
        QDBusConnection::disconnectFromBus(name);
    }
    // Calls that let this thread's event loop run, since the watcher lives in it too.
    QDBusMessage callWatcher(const QDBusConnection &bus, const QString &method, const QVariantList &arguments) {
        auto message = QDBusMessage::createMethodCall(watcherService, watcherPath, watcherService, method);
        message.setArguments(arguments);
        return bus.call(message, QDBus::BlockWithGui, 5000);
    }
    QVariant watcherProperty(const QString &name) {
        auto message = QDBusMessage::createMethodCall(watcherService, watcherPath, propertiesInterface, "Get");
        message.setArguments({QString(watcherService), name});
        const auto reply = connect().call(message, QDBus::BlockWithGui, 5000);
        return reply.type() == QDBusMessage::ReplyMessage
                   ? reply.arguments().value(0).value<QDBusVariant>().variant()
                   : QVariant();
    }
    QStringList registered() { return watcherProperty("RegisteredStatusNotifierItems").toStringList(); }

    // An application with one item and its menu, on a connection of its own.
    struct App {
        QDBusConnection bus{QString()};
        std::unique_ptr<Item> item;
        std::unique_ptr<Menu> menu;
        QString service;
        QString key() const { return service + item->path(); }
    };
    int names_ = 0;
    std::unique_ptr<App> start(const QString &title, Options options = {}) {
        auto app = std::make_unique<App>();
        app->bus = connect();
        const QString path = options.byPath ? "/org/ayatana/NotificationItem/fake" : "/StatusNotifierItem";
        app->item = std::make_unique<Item>(app->bus, path);
        app->menu = std::make_unique<Menu>(app->bus, "/MenuBar");
        app->item->getAll = options.getAll;
        app->item->activate = options.activate;
        app->item->properties = {{"Id", "fake"},
                                 {"Title", title},
                                 {"Status", "Active"},
                                 {"IconName", "fake-icon"},
                                 {"ToolTip", toolTip(title, "Some <b>bold</b> text")},
                                 {"ItemIsMenu", false},
                                 {"Menu", QVariant::fromValue(QDBusObjectPath("/MenuBar"))}};
        app->bus.registerVirtualObject(path, app->item.get());
        app->bus.registerVirtualObject("/MenuBar", app->menu.get());
        app->service = app->bus.baseService();
        if (!options.byPath) {
            app->service = QString("org.kde.StatusNotifierItem-%1-%2").arg(QCoreApplication::applicationPid()).arg(++names_);
            app->bus.registerService(app->service);
        }
        if (options.registers)
            callWatcher(app->bus, "RegisterStatusNotifierItem", {options.byPath ? path : app->service});
        return app;
    }
    void quit(std::unique_ptr<App> &app) {
        app->bus.unregisterObject(app->item->path());
        app->bus.unregisterObject(app->menu->path());
        disconnect(app->bus);
        app.reset();
    }

  private Q_SLOTS:
    void initTestCase() {
        if (QStandardPaths::findExecutable("dbus-daemon").isEmpty())
            QSKIP("dbus-daemon is not installed");
        registerTypes();
        // No service directories: a bus name nobody owns stays unowned, whatever is installed.
        QFile config(dir_.filePath("bus.conf"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write(("<busconfig><type>session</type><listen>unix:dir=" + dir_.path() +
                      "</listen><auth>EXTERNAL</auth><policy context=\"default\">"
                      "<allow send_destination=\"*\" eavesdrop=\"true\"/><allow eavesdrop=\"true\"/>"
                      "<allow own=\"*\"/></policy></busconfig>").toUtf8());
        config.close();
        bus_.start("dbus-daemon", {"--config-file=" + dir_.filePath("bus.conf"), "--nofork",
                                   "--print-address=1"});
        QVERIFY(bus_.waitForStarted());
        QVERIFY(bus_.waitForReadyRead(5000));
        address_ = QString::fromUtf8(bus_.readLine()).trimmed();
        QVERIFY2(address_.startsWith("unix:"), qPrintable(address_));
    }
    void cleanupTestCase() {
        if (bus_.state() != QProcess::NotRunning) {
            bus_.kill(); // this test's own daemon, by its process
            bus_.waitForFinished(3000);
        }
    }

    // The watcher: items by name and by path, hosts, and what goes when its owner leaves.
    void watcher() {
        TrayWatcher watcher;
        QVERIFY2(watcher.start(connect()), qPrintable(watcher.error()));
        TrayWatcher second;
        QVERIFY(!second.start(connect()));
        QVERIFY(second.error().contains("another watcher"));
        QSignalSpy added(&watcher, &TrayWatcher::StatusNotifierItemRegistered);
        QSignalSpy removed(&watcher, &TrayWatcher::StatusNotifierItemUnregistered);
        QCOMPARE(watcherProperty("ProtocolVersion").toInt(), 0);
        QCOMPARE(watcherProperty("IsStatusNotifierHostRegistered").toBool(), false);
        QVERIFY(registered().isEmpty());

        // By name: the item's object is at /StatusNotifierItem.
        auto named = connect();
        const QString name = QString("org.kde.StatusNotifierItem-%1-7").arg(QCoreApplication::applicationPid());
        QVERIFY(named.registerService(name));
        QCOMPARE(callWatcher(named, "RegisterStatusNotifierItem", {name}).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(registered(), QStringList{name + "/StatusNotifierItem"});
        QCOMPARE(added.size(), 1);
        QCOMPARE(added[0][0].toString(), name + "/StatusNotifierItem");
        // Again is no second entry.
        QCOMPARE(callWatcher(named, "RegisterStatusNotifierItem", {name}).type(), QDBusMessage::ReplyMessage);
        QCOMPARE(registered().size(), 1);

        // By path, as Ayatana's library does: the caller's connection is the service.
        auto ayatana = connect();
        QCOMPARE(callWatcher(ayatana, "RegisterStatusNotifierItem", {"/org/ayatana/NotificationItem/x"}).type(),
                 QDBusMessage::ReplyMessage);
        const QString byPath = ayatana.baseService() + "/org/ayatana/NotificationItem/x";
        QCOMPARE(registered(), (QStringList{name + "/StatusNotifierItem", byPath}));

        // Nonsense is refused: a name nobody owns, a malformed path, an empty name.
        auto other = connect();
        auto refused = callWatcher(other, "RegisterStatusNotifierItem", {"org.example.Nobody"});
        QCOMPARE(refused.type(), QDBusMessage::ErrorMessage);
        QCOMPARE(callWatcher(other, "RegisterStatusNotifierItem", {"/bad//path"}).type(), QDBusMessage::ErrorMessage);
        QCOMPARE(callWatcher(other, "RegisterStatusNotifierItem", {QString()}).type(), QDBusMessage::ErrorMessage);
        QCOMPARE(callWatcher(other, "RegisterStatusNotifierItem", {"not a name"}).type(), QDBusMessage::ErrorMessage);
        QCOMPARE(registered().size(), 2);

        // A host: registered while its name is owned.
        QSignalSpy hostAdded(&watcher, &TrayWatcher::StatusNotifierHostRegistered);
        QSignalSpy hostRemoved(&watcher, &TrayWatcher::StatusNotifierHostUnregistered);
        auto host = connect();
        QVERIFY(host.registerService("org.kde.StatusNotifierHost-test"));
        QCOMPARE(callWatcher(host, "RegisterStatusNotifierHost", {"org.kde.StatusNotifierHost-test"}).type(),
                 QDBusMessage::ReplyMessage);
        QCOMPARE(hostAdded.size(), 1);
        QCOMPARE(watcherProperty("IsStatusNotifierHostRegistered").toBool(), true);
        QCOMPARE(callWatcher(other, "RegisterStatusNotifierHost", {"org.example.NoHost"}).type(),
                 QDBusMessage::ErrorMessage);

        // Owners leaving take their items and hosts with them.
        disconnect(ayatana);
        QTRY_COMPARE(removed.size(), 1);
        QCOMPARE(removed[0][0].toString(), byPath);
        QCOMPARE(registered(), QStringList{name + "/StatusNotifierItem"});
        QVERIFY(named.unregisterService(name)); // the name goes, the connection stays
        QTRY_COMPARE(removed.size(), 2);
        QVERIFY(registered().isEmpty());
        disconnect(host);
        QTRY_COMPARE(hostRemoved.size(), 1);
        QCOMPARE(watcherProperty("IsStatusNotifierHostRegistered").toBool(), false);
        disconnect(named);
        disconnect(other);
    }
    // The name is free again once the watcher goes.
    void watcherReleasesTheName() {
        auto bus = connect();
        {
            TrayWatcher watcher;
            QVERIFY2(watcher.start(bus), qPrintable(watcher.error()));
            QVERIFY(bus.interface()->isServiceRegistered(watcherService));
        }
        QTRY_VERIFY(!bus.interface()->isServiceRegistered(watcherService));
        TrayWatcher again;
        QVERIFY2(again.start(connect()), qPrintable(again.error()));
    }

    // The host serves the watcher, reads items registered either way, follows their changes,
    // passes on clicks and the wheel, and drops items whose application leaves.
    void host() {
        TrayModel model;
        TrayHost host(model);
        QVERIFY2(host.start(connect()), qPrintable(host.error()));
        QVERIFY(host.ownsWatcher());
        QCOMPARE(host.hostName(), QString("org.kde.StatusNotifierHost-%1").arg(QCoreApplication::applicationPid()));
        QCOMPARE(watcherProperty("IsStatusNotifierHostRegistered").toBool(), true);
        auto named = start("Named");
        QTRY_COMPARE(model.count(), 1);
        const TrayItem *item = model.find(named->key());
        QVERIFY(item);
        QCOMPARE(item->title, QString("Named"));
        QCOMPARE(item->id, QString("fake"));
        QCOMPARE(item->status, QString("Active"));
        QCOMPARE(item->iconName, QString("fake-icon"));
        QCOMPARE(item->toolTipTitle, QString("Named"));
        QCOMPARE(item->toolTipText, QString("Some bold text"));
        QCOMPARE(item->menuPath, QString("/MenuBar"));
        QCOMPARE(item->itemIsMenu, false);
        QCOMPARE(model.shown(), 1);
        // Ayatana's way: by path, with neither GetAll nor Activate. It comes after, in the order
        // of registration.
        auto ayatana = start("Ayatana", {.byPath = true, .getAll = false, .activate = false});
        QTRY_COMPARE(model.count(), 2);
        QCOMPARE(model.items()[1].key, ayatana->key());
        QCOMPARE(model.items()[1].title, QString("Ayatana"));
        QCOMPARE(model.items()[0].key, named->key());

        // Changes, announced by their signals.
        named->item->change("Title", "Renamed", "NewTitle");
        QTRY_COMPARE(model.find(named->key())->title, QString("Renamed"));
        const int revision = model.find(named->key())->revision;
        named->item->change("IconName", "other-icon", "NewIcon");
        QTRY_COMPARE(model.find(named->key())->iconName, QString("other-icon"));
        QVERIFY(model.find(named->key())->revision > revision);
        named->item->change("Status", "Passive", "NewStatus", {"Passive"});
        QTRY_COMPARE(model.shown(), 1);
        QCOMPARE(model.count(), 2);
        named->item->change("Status", "NeedsAttention", "NewStatus", {"NeedsAttention"});
        QTRY_COMPARE(model.shown(), 2);
        QCOMPARE(model.find(named->key())->status, QString("NeedsAttention"));
        named->item->change("ToolTip", toolTip("Tip", "a &amp; b<br>c"), "NewToolTip");
        QTRY_COMPARE(model.find(named->key())->toolTipText, QString("a & b\nc"));
        QCOMPARE(model.find(named->key())->toolTip(), QString("Tip\na & b\nc"));
        named->item->change("IconThemePath", "/nonexistent/icons", "NewIconThemePath", {"/nonexistent/icons"});
        QTRY_COMPARE(model.find(named->key())->iconThemePath, QString("/nonexistent/icons"));
        ayatana->item->change("Title", "Ayatana again", "NewTitle"); // read with one Get each
        QTRY_COMPARE(model.find(ayatana->key())->title, QString("Ayatana again"));

        // What the panel asks reaches the item.
        model.activate(named->key(), 10, 20);
        QTRY_COMPARE(named->item->calls.value(0), QString("activate 10 20"));
        model.secondaryActivate(named->key(), 3, 4);
        model.contextMenu(named->key(), 5, 6);
        model.scroll(named->key(), -240, false);
        model.scroll(named->key(), 120, true);
        QTRY_COMPARE(named->item->calls.size(), 5);
        QCOMPARE(named->item->calls, (QStringList{"activate 10 20", "secondary 3 4", "context 5 6",
                                                  "scroll -240 vertical", "scroll 120 horizontal"}));
        // An item without Activate asks the panel for its menu instead.
        QSignalSpy refused(&model, &TrayModel::activationRefused);
        model.activate(ayatana->key(), 1, 2);
        QTRY_COMPARE(refused.size(), 1);
        QCOMPARE(refused[0][0].toString(), ayatana->key());

        // An application quitting, or giving up its name, takes its item away.
        quit(ayatana);
        QTRY_COMPARE(model.count(), 1);
        QCOMPARE(registered(), QStringList{named->key()});
        named->bus.unregisterService(named->service);
        QTRY_COMPARE(model.count(), 0);
        quit(named);
    }
    // Properties of the wrong type, or missing, take their defaults.
    void mistypedProperties() {
        TrayModel model;
        TrayHost host(model);
        QVERIFY(host.start(connect()));
        auto app = start("Odd", {.registers = false});
        app->item->properties = {{"Id", 7},
                                 {"Title", QStringList{"a", "b"}},
                                 {"Status", "Sleeping"},
                                 {"IconName", true},
                                 {"ToolTip", "just a string"},
                                 {"ItemIsMenu", "yes"},
                                 {"Menu", 42},
                                 {"IconPixmap", "not pixels"}};
        callWatcher(app->bus, "RegisterStatusNotifierItem", {app->service});
        QTRY_COMPARE(model.count(), 1);
        const TrayItem *item = model.find(app->key());
        QCOMPARE(item->id, QString());
        QCOMPARE(item->title, QString());
        QCOMPARE(item->status, QString("Active"));
        QCOMPARE(item->iconName, QString());
        QCOMPARE(item->toolTip(), QString());
        QCOMPARE(item->itemIsMenu, false);
        QCOMPARE(item->menuPath, QString());
        // A menu given as a string path is taken; Chromium's "no menu" path is none.
        app->item->change("Menu", "/Some/Menu", "NewMenu");
        QTRY_COMPARE(model.find(app->key())->menuPath, QString("/Some/Menu"));
        app->item->change("Menu", QVariant::fromValue(QDBusObjectPath("/NO_DBUSMENU")), "NewMenu");
        QTRY_COMPARE(model.find(app->key())->menuPath, QString());
        quit(app);
    }
    // IconPixmap is ARGB32 in network byte order; the size nearest the panel's is picked, a named
    // icon wins over pixels, the attention icon shows while the item needs attention, and the
    // overlay sits in the bottom right corner.
    void pictures() {
        auto solid = [](const QColor &color, int size) {
            QImage image(size, size, QImage::Format_ARGB32);
            image.fill(color);
            return image;
        };
        QByteArray bytes;
        for (int i = 0; i < 4; ++i)
            bytes.append("\x80\x11\x22\x33", 4); // A, R, G, B
        QCOMPARE(trayImageFromArgb32(2, 2, bytes).pixelColor(1, 1), QColor(0x11, 0x22, 0x33, 0x80));
        QVERIFY(trayImageFromArgb32(2, 2, bytes.left(15)).isNull());
        QVERIFY(trayImageFromArgb32(0, 2, bytes).isNull());
        QVERIFY(trayImageFromArgb32(-1, -1, bytes).isNull());
        QVERIFY(trayImageFromArgb32(100000, 100000, bytes).isNull());
        const QList<QImage> sizes{solid(Qt::red, 16), solid(Qt::green, 32), solid(Qt::blue, 64)};
        QCOMPARE(trayPickPixmap(sizes, {22, 22}).pixelColor(5, 5), QColor(Qt::green));
        QCOMPARE(trayPickPixmap(sizes, {22, 22}).size(), QSize(22, 22));
        QCOMPARE(trayPickPixmap(sizes, {16, 16}).pixelColor(5, 5), QColor(Qt::red));
        QCOMPARE(trayPickPixmap(sizes, {100, 100}).pixelColor(5, 5), QColor(Qt::blue));
        QCOMPARE(trayPickPixmap(sizes, {8, 8}).pixelColor(1, 1), QColor(Qt::red));
        QVERIFY(trayPickPixmap({}, {22, 22}).isNull());

        TrayModel model;
        TrayHost host(model);
        QVERIFY(host.start(connect()));
        auto app = start("Pictures", {.registers = false});
        // Sizes in any order, one of them malformed.
        app->item->properties["IconName"] = "";
        app->item->properties["IconPixmap"] = QVariant::fromValue(
            QList<Pixmap>{square(Qt::blue, 64), {30, 30, "short"}, square(Qt::red, 16), square(Qt::green, 32)});
        callWatcher(app->bus, "RegisterStatusNotifierItem", {app->service});
        QTRY_COMPARE(model.count(), 1);
        const int serial = model.find(app->key())->serial;
        QCOMPARE(model.find(app->key())->icon.size(), 3);
        QCOMPARE(model.picture(serial, {22, 22}).pixelColor(11, 11), QColor(Qt::green));
        QCOMPARE(model.picture(serial, {64, 64}).pixelColor(11, 11), QColor(Qt::blue));
        app->item->properties["AttentionIconPixmap"] = pixmaps(Qt::yellow, {22});
        app->item->change("Status", "NeedsAttention", "NewStatus", {"NeedsAttention"});
        QTRY_COMPARE(model.picture(serial, {22, 22}).pixelColor(11, 11), QColor(Qt::yellow));
        app->item->change("Status", "Active", "NewStatus", {"Active"});
        QTRY_COMPARE(model.picture(serial, {22, 22}).pixelColor(11, 11), QColor(Qt::green));
        app->item->change("OverlayIconPixmap", pixmaps(Qt::white, {11}), "NewOverlayIcon");
        QTRY_COMPARE(model.picture(serial, {22, 22}).pixelColor(20, 20), QColor(Qt::white));
        QCOMPARE(model.picture(serial, {22, 22}).pixelColor(2, 2), QColor(Qt::green));
        app->item->change("OverlayIconPixmap", QVariant(), "NewOverlayIcon");
        QTRY_COMPARE(model.picture(serial, {22, 22}).pixelColor(20, 20), QColor(Qt::green));

        // A name in the item's own folder, or in a theme laid out in it (the largest), or a file
        // by absolute path wins over the pixels.
        QTemporaryDir icons;
        QVERIFY(solid(Qt::cyan, 22).save(icons.filePath("probe-icon.png")));
        QVERIFY(QDir(icons.path()).mkpath("hicolor/22x22/apps") && QDir(icons.path()).mkpath("hicolor/48x48/apps"));
        QVERIFY(solid(Qt::magenta, 22).save(icons.filePath("hicolor/22x22/apps/nested.png")));
        QVERIFY(solid(Qt::darkMagenta, 48).save(icons.filePath("hicolor/48x48/apps/nested.png")));
        app->item->properties["IconThemePath"] = icons.path();
        app->item->change("IconName", "probe-icon", "NewIcon");
        QTRY_COMPARE(model.picture(serial, {22, 22}).pixelColor(11, 11), QColor(Qt::cyan));
        app->item->change("IconName", "nested", "NewIcon");
        QTRY_COMPARE(model.picture(serial, {22, 22}).pixelColor(11, 11), QColor(Qt::darkMagenta));
        QVERIFY(solid(Qt::darkCyan, 22).save(icons.filePath("absolute.png")));
        app->item->change("IconName", icons.filePath("absolute.png"), "NewIcon");
        QTRY_COMPARE(model.picture(serial, {22, 22}).pixelColor(11, 11), QColor(Qt::darkCyan));
        // A name found nowhere, a relative path or a pipe falls back on the pixels; the pipe is
        // never read.
        QCOMPARE(mkfifo(QFile::encodeName(icons.filePath("pipe.png")).constData(), 0600), 0);
        for (const QString &name : {QString("no-such-icon-anywhere"), QString("../probe-icon"), icons.filePath("pipe.png")}) {
            app->item->change("IconName", name, "NewIcon");
            QTRY_COMPARE(model.find(app->key())->iconName, name);
            QCOMPARE(model.picture(serial, {22, 22}).pixelColor(11, 11), QColor(Qt::green));
        }
        QCOMPARE(trayIconFile("probe", "/"), QString()); // a bounded look, even from the root
        // Without any icon there is no picture: the panel draws a stand-in.
        app->item->properties.remove("IconPixmap");
        app->item->change("IconName", "", "NewIcon");
        QTRY_VERIFY(model.picture(serial, {22, 22}).isNull());
        quit(app);
    }
    // Another program serves the watcher: the host registers with it and follows its items, and
    // serves the name itself once that program goes.
    void hostOfAnotherWatcher() {
        auto other = std::make_unique<TrayWatcher>();
        QVERIFY(other->start(connect()));
        auto early = start("Early");
        TrayModel model;
        TrayHost host(model);
        QVERIFY(host.start(connect()));
        QVERIFY(!host.ownsWatcher());
        QTRY_VERIFY(other->hostRegistered());
        QTRY_COMPARE(model.count(), 1);
        auto late = start("Late", {.byPath = true});
        QTRY_COMPARE(model.count(), 2);
        quit(late);
        QTRY_COMPARE(model.count(), 1);
        other.reset();
        QTRY_VERIFY(host.ownsWatcher());
        QCOMPARE(model.count(), 1);
        // The item registers again with the new watcher, as items do; it is not shown twice.
        callWatcher(early->bus, "RegisterStatusNotifierItem", {early->service});
        QCOMPARE(registered(), QStringList{early->key()});
        QCOMPARE(model.count(), 1);
        quit(early);
        QTRY_COMPARE(model.count(), 0);
    }
};
QTEST_MAIN(TrayDbusTest)
#include "tray_dbus_test.moc"
