// SPDX-License-Identifier: GPL-3.0-or-later
#include "task_filter.hpp"
#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>
#include <algorithm>
#include <functional>
#include <iostream>
#include <vector>

namespace {
// The roles and change signals of the shell's TaskModel, counting how often the filters read it.
class FakeTasks : public QAbstractListModel {
  public:
    enum Role { TaskId = Qt::UserRole + 1, Title, AppId, Active, Minimized, Maximized, Urgent };
    struct Row {
        int id;
        QString title, appId;
        bool active = false, minimized = false, urgent = false;
    };
    std::vector<Row> rows;
    mutable int reads = 0;
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : int(rows.size());
    }
    QVariant data(const QModelIndex &index, int role) const override {
        ++reads;
        const auto &row = rows[size_t(index.row())];
        switch (role) {
        case TaskId: return row.id;
        case Title: return row.title;
        case AppId: return row.appId;
        case Active: return row.active;
        case Minimized: return row.minimized;
        case Maximized: return false;
        case Urgent: return row.urgent;
        }
        return {};
    }
    QHash<int, QByteArray> roleNames() const override {
        return {{TaskId, "taskId"}, {Title, "title"},         {AppId, "appId"},
                {Active, "active"}, {Minimized, "minimized"}, {Maximized, "maximized"},
                {Urgent, "urgent"}};
    }
    void add(const QString &app, const QString &title) {
        beginInsertRows({}, int(rows.size()), int(rows.size()));
        rows.push_back({nextId++, title, app});
        endInsertRows();
    }
    void remove(int row) {
        beginRemoveRows({}, row, row);
        rows.erase(rows.begin() + row);
        endRemoveRows();
    }
    void changed(int row, const QList<int> &roles) {
        Q_EMIT dataChanged(index(row), index(row), roles);
    }
    void reset(size_t keep, const std::function<void(FakeTasks &)> &edit) {
        beginResetModel();
        rows.resize(keep);
        edit(*this);
        endResetModel();
    }
    void moveLastToFront() {
        beginMoveRows({}, int(rows.size()) - 1, int(rows.size()) - 1, {}, 0);
        std::rotate(rows.begin(), rows.end() - 1, rows.end());
        endMoveRows();
    }
    int nextId = 1;
};
} // namespace

class TaskFilterTest : public QObject {
    Q_OBJECT
    FakeTasks source;
    std::vector<std::unique_ptr<TaskFilter>> filters;

    QList<int> appIds(TaskFilter &filter) {
        QList<int> ids;
        for (int row = 0; row < filter.rowCount(); ++row)
            ids.push_back(filter.data(filter.index(row, 0), FakeTasks::TaskId).toInt());
        return ids;
    }

  private Q_SLOTS:
    void init() {
        source.rows.clear();
        source.nextId = 1;
        filters.clear();
        // Twenty applications with three windows each, as a busy desktop has.
        for (int window = 0; window < 3; ++window)
            for (int app = 0; app < 20; ++app)
                source.add(QString("app%1").arg(app), QString("window %1").arg(window));
        // The taskbar's own filter and one for each of a few stacked buttons.
        for (int i = 0; i < 8; ++i) {
            auto filter = std::make_unique<TaskFilter>();
            filter->setGrouped(true);
            filter->setSourceModel(&source);
            filters.push_back(std::move(filter));
        }
    }
    void groupsOneWindowPerApplication() {
        QCOMPARE(filters[0]->rowCount(), 20);
        QCOMPARE(appIds(*filters[0]).first(), 1);
        // Removing the first window of an application promotes the next one, which stands
        // where that window is, after the other applications' first windows.
        source.remove(0);
        QCOMPARE(filters[0]->rowCount(), 20);
        QCOMPARE(appIds(*filters[0]).first(), 2);
        QCOMPARE(appIds(*filters[0]).last(), 21);
        // A window changing application moves between groups.
        source.rows[5].appId = "app0";
        source.changed(5, {FakeTasks::AppId});
        QCOMPARE(filters[0]->rowCount(), 20);
        source.rows[25].appId = "brand-new";
        source.changed(25, {FakeTasks::AppId});
        QCOMPARE(filters[0]->rowCount(), 21);
    }
    // A window's title changing is the commonest event of all (a terminal, a browser tab, a
    // media player), and must not make every filter look at every window again.
    void titleChangesDoNotRefilter() {
        const int before = source.reads;
        QSignalSpy counts(filters[0].get(), &TaskFilter::countChanged);
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 100; ++i) {
            source.rows[size_t(i % 60)].title = QString("title %1").arg(i);
            source.changed(i % 60, {FakeTasks::Title});
        }
        std::cerr << "100 title changes: " << timer.nsecsElapsed() / 1000 << " us, "
                  << (source.reads - before) << " reads\n";
        QVERIFY2(source.reads - before < 100 * 8 * 2, "title changes made the filters re-read everything");
        QCOMPARE(counts.count(), 0);
    }
    void followsResetsAndMoves() {
        source.reset(4, [](FakeTasks &tasks) { tasks.rows[1].appId = tasks.rows[0].appId; });
        QCOMPARE(filters[0]->rowCount(), 3);
        QCOMPARE(filters[3]->rowCount(), 3);
        source.moveLastToFront();
        QCOMPARE(filters[0]->rowCount(), 3);
        QCOMPARE(appIds(*filters[0]).first(), source.rows[0].id);
    }
    void stateChangesUpdateTheSummary() {
        QSignalSpy summaries(filters[0].get(), &TaskFilter::summaryChanged);
        source.rows[0].active = true;
        source.changed(0, {FakeTasks::Active});
        QVERIFY(summaries.count() >= 1);
        QCOMPARE(filters[0]->activeTask(), 1);
        source.rows[0].active = false;
        source.changed(0, {FakeTasks::Active});
        QCOMPARE(filters[0]->activeTask(), -1);
    }
    // A stacked button is urgent when any window of its application is.
    void urgencyOfAStackedButton() {
        TaskFilter app3, app4;
        app3.setSourceModel(&source);
        app3.setWindowApp("app3");
        app4.setSourceModel(&source);
        app4.setWindowApp("app4");
        QCOMPARE(app3.rowCount(), 3);
        QVERIFY(!app3.urgent() && !app4.urgent() && !filters[0]->urgent());
        QSignalSpy summaries(&app3, &TaskFilter::summaryChanged);
        source.rows[43].urgent = true; // the third window of app3
        source.changed(43, {FakeTasks::Urgent});
        QVERIFY(summaries.count() >= 1);
        QVERIFY(app3.urgent());
        QVERIFY(!app4.urgent());
        source.rows[43].urgent = false;
        source.changed(43, {FakeTasks::Urgent});
        QVERIFY(!app3.urgent());
    }
};
QTEST_GUILESS_MAIN(TaskFilterTest)
#include "task_filter_test.moc"
