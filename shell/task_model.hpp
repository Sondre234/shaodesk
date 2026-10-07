// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ext-image-capture-source-v1-client-protocol.h" // before the next, which names its interface
#include "shaodesk-window-control-v1-client-protocol.h"
#include "window_pictures.hpp"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include <QAbstractListModel>
#include <QSocketNotifier>
#include <memory>
#include <vector>
#include <wayland-client.h>

// The windows the compositor lists for taskbars (wlr-foreign-toplevel), with where each is from
// shaodesk-window-control-v1 when the compositor offers it: `output` (connector name), `workspace`
// (of that output, from 1; 0 until it is known), whether it is `sticky` or `floating`, and whether
// its workspace is `tiling`. From its version 2, a window watched with watchPicture also has a
// `picture` (WindowPictures), "" until one has arrived.
class TaskModel : public QAbstractListModel {
    Q_OBJECT
  public:
    enum Role {
        TaskId = Qt::UserRole + 1,
        Title,
        AppId,
        Active,
        Minimized,
        Maximized,
        Urgent,
        Fullscreen,
        Output,
        Workspace,
        Sticky,
        Floating,
        Tiling,
        Picture
    };
    explicit TaskModel(QObject *parent = nullptr);
    ~TaskModel() override;
    bool connectDisplay();
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE void activate(int id);
    Q_INVOKABLE void minimize(int id);
    Q_INVOKABLE void maximize(int id);
    Q_INVOKABLE void setFullscreen(int id, bool fullscreen);
    // Through the window control, which these need: to workspace `number` (from 1) of the output
    // the window is on, onto another output, shown on every workspace, kept out of the tiling.
    // None of them focuses the window.
    Q_INVOKABLE void moveToWorkspace(int id, int number);
    Q_INVOKABLE void moveToOutput(int id, const QString &output);
    Q_INVOKABLE void setSticky(int id, bool sticky);
    Q_INVOKABLE void setFloating(int id, bool floating);
    Q_INVOKABLE void close(int id);
    Q_INVOKABLE void showDesktop();
    // Counted per window: while watched, the window is pictured, once or as it redraws when
    // `live`, at `pixelWidth` device pixels wide. Its last picture stays until it closes.
    Q_INVOKABLE void watchPicture(int taskId, int pixelWidth, bool live);
    Q_INVOKABLE void unwatchPicture(int taskId);
    // The window's last picture, for the image provider on any thread; null while it has none.
    QImage picture(int taskId) const;
    // The windows the compositor says are asking for attention, as {appId, title} pairs: the
    // foreign-toplevel protocol has no such state, so a task is urgent when a pair matches its
    // app id and title (each pair marks one task, the first not marked already).
    void setUrgent(const QList<QPair<QString, QString>> &windows);
    // Same signature as ListModel.move, so the panel can reorder either.
    Q_INVOKABLE void move(int from, int to, int count = 1);
  Q_SIGNALS:
    void disconnected();

  private:
    // What a window is, as the compositor last said.
    struct State {
        QString title, appId, output;
        bool active = false, minimized = false, maximized = false, fullscreen = false, urgent = false;
        int workspace = 0;
        bool sticky = false, floating = false, tiling = false;
    };
    struct Task {
        TaskModel *model;
        zwlr_foreign_toplevel_handle_v1 *handle;
        shaodesk_window_v1 *window = nullptr;
        int id;
        State state;
        // What the model last announced; a `done` that changes none of it announces nothing.
        State shown;
    };
    std::vector<std::unique_ptr<Task>> tasks_;
    wl_display *display_ = nullptr;
    wl_registry *registry_ = nullptr;
    wl_seat *seat_ = nullptr;
    zwlr_foreign_toplevel_manager_v1 *manager_ = nullptr;
    shaodesk_window_control_v1 *control_ = nullptr;
    wl_shm *shm_ = nullptr;
    ext_image_copy_capture_manager_v1 *captureManager_ = nullptr;
    std::unique_ptr<QSocketNotifier> read_, write_;
    QList<QPair<QString, QString>> urgent_;
    int nextId_ = 1;
    WindowPictures pictures_{[this] { flush(); }};
    Task *find(int id);
    void flush();
    // Works out which tasks are urgent; those but `except` that change announce it.
    void matchUrgent(const Task *except = nullptr);
    void changed(Task *task);
    // Asks the compositor where the task's window is, and to keep saying.
    void watch(Task *task);
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
    static void windowOutput(void *, shaodesk_window_v1 *, const char *);
    static void windowWorkspace(void *, shaodesk_window_v1 *, uint32_t);
    static void windowState(void *, shaodesk_window_v1 *, uint32_t);
    static void windowDone(void *, shaodesk_window_v1 *);
};
