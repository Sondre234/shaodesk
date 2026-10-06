// SPDX-License-Identifier: GPL-3.0-or-later
// The start menu's model: the launch history.
#include "launch_history.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

namespace {
QString read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}
bool write(const QString &path, const QByteArray &text) {
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
}
QDateTime at(qint64 seconds) { return QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC); }
} // namespace

class StartMenuTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void recordsLaunches() {
        QTemporaryDir directory;
        const auto path = directory.filePath("state/shaodesk/launches");
        LaunchHistory history(path);
        QVERIFY(history.entries().isEmpty());
        QVERIFY(history.record("firefox.desktop", at(1000)));
        QVERIFY(history.record("foot.desktop", at(2000)));
        QVERIFY(history.record("firefox.desktop", at(3000)));
        // The most recent first, with how often; saved as it goes, in a folder made for it.
        QCOMPARE(read(path), QString("firefox.desktop\t2\t3000\nfoot.desktop\t1\t2000\n"));
        QCOMPARE(history.find("firefox.desktop")->count, 2);
        QCOMPARE(history.find("firefox.desktop")->last, at(3000));
        QVERIFY(!history.find("kate.desktop"));
        // Read back as it was saved.
        LaunchHistory again(path);
        QCOMPARE(again.entries().size(), 2);
        QCOMPARE(again.entries()[0].id, QString("firefox.desktop"));
        QCOMPARE(again.entries()[1].last, at(2000));
        // Written through a temporary file, nothing else is left in the folder.
        QCOMPARE(QDir(directory.filePath("state/shaodesk")).entryList(QDir::Files),
                 QStringList{"launches"});
    }
    void readsWhatItCan() {
        QTemporaryDir directory;
        const auto path = directory.filePath("launches");
        QVERIFY(write(path, "kate.desktop\t3\t100\nbroken line\nfoot.desktop\tx\t5\n"
                            "gimp.desktop\t1\t900\nkate.desktop\t9\t50\n\t1\t5\nmpv.desktop\t0\t5\n"));
        LaunchHistory history(path);
        // Bad lines and a second line for the same application are left out, and the rest
        // sorted by their last launch.
        QCOMPARE(history.entries().size(), 2);
        QCOMPARE(history.entries()[0].id, QString("gimp.desktop"));
        QCOMPARE(history.entries()[1].id, QString("kate.desktop"));
        QCOMPARE(history.entries()[1].count, 3);
    }
    void keepsTheMostRecent() {
        QTemporaryDir directory;
        const auto path = directory.filePath("launches");
        LaunchHistory history(path);
        for (int i = 0; i < LaunchHistory::limit + 20; ++i)
            QVERIFY(history.record(QString("app%1.desktop").arg(i), at(1000 + i)));
        QCOMPARE(history.entries().size(), LaunchHistory::limit);
        QCOMPARE(history.entries().first().id, QString("app%1.desktop").arg(LaunchHistory::limit + 19));
        QVERIFY(!history.find("app0.desktop"));
        QCOMPARE(read(path).count('\n'), LaunchHistory::limit);
    }
    void saysWhenItCannotSave() {
        QTemporaryDir directory;
        // Its folder is a file.
        QVERIFY(write(directory.filePath("state"), "not a folder"));
        LaunchHistory history(directory.filePath("state/launches"));
        QVERIFY(!history.record("foot.desktop", at(10)));
        QVERIFY(!history.error().isEmpty());
        // What it could not save is still known while the shell runs.
        QCOMPARE(history.find("foot.desktop")->count, 1);
    }
};
QTEST_APPLESS_MAIN(StartMenuTest)
#include "start_menu_test.moc"
