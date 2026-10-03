// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include <QAbstractListModel>
#include <QSocketNotifier>
#include <memory>
#include <vector>
#include <wayland-client.h>

class TaskModel : public QAbstractListModel {
    Q_OBJECT
  public:
    enum Role { TaskId = Qt::UserRole + 1, Title, AppId, Active, Minimized, Maximized, Urgent };
    explicit TaskModel(QObject *parent = nullptr);
    ~TaskModel() override;
    bool connectDisplay();
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE void activate(int id);
    Q_INVOKABLE void minimize(int id);
    Q_INVOKABLE void maximize(int id);
    Q_INVOKABLE void close(int id);
    Q_INVOKABLE void showDesktop();
    // The windows the compositor says are asking for attention, as {appId, title} pairs: the
    // foreign-toplevel protocol has no such state, so a task is urgent when a pair matches its
    // app id and title (each pair marks one task, the first not marked already).
    void setUrgent(const QList<QPair<QString, QString>> &windows);
    // Same signature as ListModel.move, so the panel can reorder either.
    Q_INVOKABLE void move(int from, int to, int count = 1);
  Q_SIGNALS:
    void disconnected();

  private:
    struct Task {
        TaskModel *model;
        zwlr_foreign_toplevel_handle_v1 *handle;
        int id;
        QString title, appId;
        bool active = false, minimized = false, maximized = false, urgent = false;
        // What the model last announced; a `done` that changes none of it announces nothing.
        QString shownTitle, shownAppId;
        bool shownActive = false, shownMinimized = false, shownMaximized = false, shownUrgent = false;
    };
    std::vector<std::unique_ptr<Task>> tasks_;
    wl_display *display_ = nullptr;
    wl_registry *registry_ = nullptr;
    wl_seat *seat_ = nullptr;
    zwlr_foreign_toplevel_manager_v1 *manager_ = nullptr;
    std::unique_ptr<QSocketNotifier> read_, write_;
    QList<QPair<QString, QString>> urgent_;
    int nextId_ = 1;
    Task *find(int id);
    void flush();
    // Works out which tasks are urgent; those but `except` that change announce it.
    void matchUrgent(const Task *except = nullptr);
    void changed(Task *task);
    void removed(Task *task);
    static void global(void *, wl_registry *, uint32_t, const char *, uint32_t);
    static void globalRemoved(void *, wl_registry *, uint32_t);
    static void newTask(void *, zwlr_foreign_toplevel_manager_v1 *,
                        zwlr_foreign_toplevel_handle_v1 *);
    static void finished(void *, zwlr_foreign_toplevel_manager_v1 *);
    static void title(void *, zwlr_foreign_toplevel_handle_v1 *, const char *);
    static void appId(void *, zwlr_foreign_toplevel_handle_v1 *, const char *);
    static void output(void *, zwlr_foreign_toplevel_handle_v1 *, wl_output *);
    static void state(void *, zwlr_foreign_toplevel_handle_v1 *, wl_array *);
    static void done(void *, zwlr_foreign_toplevel_handle_v1 *);
    static void closed(void *, zwlr_foreign_toplevel_handle_v1 *);
};
