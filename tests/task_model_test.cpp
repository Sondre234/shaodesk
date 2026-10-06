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
        // A window has no picture until it is watched. Through a window control older than
        // version 2 it never has one, and watching asks the compositor nothing, so everything
        // below still works.
        if (model.roleNames().value(TaskModel::Picture) != "picture" ||
            !value(TaskModel::Picture).toString().isEmpty() || !model.picture(id).isNull())
            throw std::runtime_error("the window has a picture before one was asked for");
        model.watchPicture(id, 160, true);
        model.watchPicture(id + 100, 160, true); // no such window
        model.unwatchPicture(id);
        model.unwatchPicture(id); // one more than it was watched
        // Where it is comes from the compositor's window control: its only output, the first
        // workspace, floating since the example configuration does not tile.
        wait([&] { return value(TaskModel::Workspace).toInt() == 1; }, "no workspace arrived");
        if (value(TaskModel::Output).toString() != "HEADLESS-1" ||
            value(TaskModel::Sticky).toBool() || value(TaskModel::Tiling).toBool() ||
            model.roleNames().value(TaskModel::Workspace) != "workspace" ||
            model.roleNames().value(TaskModel::Tiling) != "tiling")
            throw std::runtime_error("the window's place is not as the compositor has it");
        // The window menu's requests come back as the window's new place.
        model.moveToWorkspace(id, 3);
        wait([&] { return value(TaskModel::Workspace).toInt() == 3; }, "moving to a workspace failed");
        model.setSticky(id, true);
        wait([&] { return value(TaskModel::Sticky).toBool() && value(TaskModel::Workspace) == 1; },
             "making the window sticky failed");
        if (!value(TaskModel::Floating).toBool())
            throw std::runtime_error("a sticky window does not float");
        model.setSticky(id, false);
        wait([&] { return !value(TaskModel::Sticky).toBool(); }, "unsticking failed");
        model.setFloating(id, false);
        wait([&] { return !value(TaskModel::Floating).toBool(); }, "letting the window tile failed");
        model.setFloating(id, true);
        wait([&] { return value(TaskModel::Floating).toBool(); }, "floating the window failed");
        model.moveToOutput(id, "HEADLESS-1"); // the one it is on: nothing changes
        // Moved away, it lost the focus, and none of these gave it back.
        if (value(TaskModel::Active).toBool())
            throw std::runtime_error("a request of the window menu focused the window");
        model.activate(id);
        wait([&] { return value(TaskModel::Active).toBool(); }, "activation failed");
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
            << "Task model metadata, place, minimize, restore, maximize, fullscreen, show desktop, "
               "close passed\n";
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
