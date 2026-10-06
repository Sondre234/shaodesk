// SPDX-License-Identifier: GPL-3.0-or-later
#include "task_model.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QThread>
#include <functional>
#include <iostream>
#include <stdexcept>

// Exercise the model used by QML against an actual compositor and xdg client.
// No fake protocol callbacks: every state change must come back over Wayland.
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QProcess client;
    try {
        if (argc != 2)
            throw std::runtime_error("expected Wayland probe executable");
        TaskModel model;
        if (!model.connectDisplay())
            throw std::runtime_error("cannot connect task model");
        auto wait = [&](const std::function<bool()> &condition, const char *message) {
            QElapsedTimer timer;
            timer.start();
            while (!condition()) {
                QCoreApplication::processEvents();
                if (timer.elapsed() > 2000)
                    throw std::runtime_error(message);
                QThread::msleep(1);
            }
        };
        client.start(QString::fromLocal8Bit(argv[1]), {"--external-control"});
        if (!client.waitForStarted(2000))
            throw std::runtime_error("cannot start probe client");
        auto value = [&](int role) { return model.data(model.index(0), role); };
        wait([&] { return model.rowCount() == 1 && value(TaskModel::Active).toBool(); },
             "task did not appear with active state");
        if (value(TaskModel::Title).toString() != "shaodesk protocol probe" ||
            value(TaskModel::AppId).toString() != "shaodesk-probe")
            throw std::runtime_error("task metadata incorrect");
        int id = value(TaskModel::TaskId).toInt();
        model.activate(id); // Clicking the active task minimizes it.
        wait([&] { return value(TaskModel::Minimized).toBool(); }, "minimize failed");
        model.activate(id);
        wait(
            [&] {
                return value(TaskModel::Active).toBool() && !value(TaskModel::Minimized).toBool();
            },
            "restore/activate failed");
        model.maximize(id);
        wait([&] { return value(TaskModel::Maximized).toBool(); }, "maximize failed");
        model.maximize(id);
        wait([&] { return !value(TaskModel::Maximized).toBool(); }, "unmaximize failed");
        model.setFullscreen(id, true);
        wait([&] { return value(TaskModel::Fullscreen).toBool(); }, "fullscreen failed");
        model.setFullscreen(id, false);
        wait([&] { return !value(TaskModel::Fullscreen).toBool(); }, "leaving fullscreen failed");
        if (model.roleNames().value(TaskModel::Fullscreen) != "fullscreen")
            throw std::runtime_error("the fullscreen role is not named for QML");
        model.showDesktop();
        wait([&] { return value(TaskModel::Minimized).toBool(); }, "show desktop failed");
        model.activate(id);
        wait([&] { return value(TaskModel::Active).toBool(); }, "second activation failed");
        // The compositor names urgent windows by app id and title; a match marks the task, and
        // only that announces a change of the role.
        int urgentSignals = 0;
        QObject::connect(&model, &QAbstractItemModel::dataChanged, &model,
                         [&](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                             if (roles.contains(TaskModel::Urgent))
                                 ++urgentSignals;
                         });
        if (value(TaskModel::Urgent).toBool())
            throw std::runtime_error("a task is urgent before anything asked");
        model.setUrgent({{"shaodesk-probe", "another title"}, {"other-app", "shaodesk protocol probe"}});
        if (value(TaskModel::Urgent).toBool() || urgentSignals != 0)
            throw std::runtime_error("an urgent window with another title or app marked the task");
        model.setUrgent({{"shaodesk-probe", "shaodesk protocol probe"}});
        if (!value(TaskModel::Urgent).toBool() || urgentSignals != 1)
            throw std::runtime_error("the urgent window did not mark its task");
        model.setUrgent({{"shaodesk-probe", "shaodesk protocol probe"}});
        if (urgentSignals != 1)
            throw std::runtime_error("an unchanged urgent list announced a change");
        model.setUrgent({});
        if (value(TaskModel::Urgent).toBool() || urgentSignals != 2)
            throw std::runtime_error("clearing the urgent list did not unmark the task");
        if (model.roleNames().value(TaskModel::Urgent) != "urgent")
            throw std::runtime_error("the urgent role is not named for QML");
        model.setUrgent({{"shaodesk-probe", "shaodesk protocol probe"}});
        model.close(id);
        wait([&] { return model.rowCount() == 0; }, "closed task was not removed");
        if ((client.state() != QProcess::NotRunning && !client.waitForFinished(2000)) ||
            client.exitStatus() != QProcess::NormalExit || client.exitCode() != 0)
            throw std::runtime_error("probe did not exit cleanly");
        // Stale IDs must be harmless after a window closes.
        model.activate(id);
        model.close(id);
        std::cout
            << "Task model metadata, minimize, restore, maximize, fullscreen, show desktop, close "
               "passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n' << client.readAllStandardError().toStdString();
        if (client.state() != QProcess::NotRunning) {
            client.kill();
            client.waitForFinished(2000);
        }
        return 1;
    }
}
