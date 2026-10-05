// SPDX-License-Identifier: GPL-3.0-or-later
// The system tray on a private session bus: a dbus-daemon this test starts and kills, never the
// real one. Fake items (tests/tray_fake_item.hpp), each on a connection of its own, play the
// applications.
#include "tray_fake_item.hpp"
#include "tray_watcher.hpp"
#include <QDBusConnectionInterface>
#include <QDir>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

using namespace faketray;

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
};
QTEST_MAIN(TrayDbusTest)
#include "tray_dbus_test.moc"
