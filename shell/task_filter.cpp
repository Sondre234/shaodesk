// SPDX-License-Identifier: GPL-3.0-or-later
#include "task_filter.hpp"
#include "controller.hpp"

TaskFilter::TaskFilter(QObject *parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
    connect(this, &QAbstractItemModel::rowsInserted, this, &TaskFilter::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &TaskFilter::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &TaskFilter::countChanged);
    connect(this, &QAbstractItemModel::rowsInserted, this, &TaskFilter::summaryChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &TaskFilter::summaryChanged);
    connect(this, &QAbstractItemModel::rowsMoved, this, &TaskFilter::summaryChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &TaskFilter::summaryChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &TaskFilter::summaryChanged);
    connect(this, &QAbstractItemModel::dataChanged, this, &TaskFilter::summaryChanged);
}
void TaskFilter::setSourceModel(QAbstractItemModel *source) {
    for (const auto &connection : sourceConnections_)
        disconnect(connection);
    sourceConnections_.clear();
    QSortFilterProxyModel::setSourceModel(source);
    if (!source)
        return;
    // Connected after the proxy's own handlers, so it has caught up with the change first. A
    // window whose app id changes may join or leave this slot; grouped, which window stands for
    // its application depends on those ahead of it too.
    auto regroup = [this] {
        if (grouped_)
            refilter();
    };
    sourceConnections_ = {
        connect(source, &QAbstractItemModel::dataChanged, this, &TaskFilter::refilter),
        connect(source, &QAbstractItemModel::rowsInserted, this, regroup),
        connect(source, &QAbstractItemModel::rowsRemoved, this, regroup),
        connect(source, &QAbstractItemModel::rowsMoved, this, regroup),
    };
}
void TaskFilter::refilter() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    endFilterChange(Direction::Rows);
#else
    invalidateRowsFilter();
#endif
}
QObject *TaskFilter::controller() const { return shell_; }
void TaskFilter::setController(QObject *controller) {
    if (shell_ == controller)
        return;
    if (shell_)
        disconnect(shell_, nullptr, this, nullptr);
    shell_ = qobject_cast<ShellController *>(controller);
    // Pinning and unpinning move windows between slots.
    if (shell_)
        connect(shell_, &ShellController::appsChanged, this, &TaskFilter::refilter);
    refilter();
    Q_EMIT controllerChanged();
}
void TaskFilter::setApp(const QString &app) {
    if (app_ == app)
        return;
    app_ = app;
    refilter();
    Q_EMIT appChanged();
}
void TaskFilter::setWindowApp(const QString &windowApp) {
    if (windowApp_ == windowApp)
        return;
    windowApp_ = windowApp;
    refilter();
    Q_EMIT windowAppChanged();
}
void TaskFilter::setGrouped(bool grouped) {
    if (grouped_ == grouped)
        return;
    grouped_ = grouped;
    refilter();
    Q_EMIT groupedChanged();
}
QVariant TaskFilter::sourceValue(int row, const char *role) const {
    auto *source = sourceModel();
    return source->data(source->index(row, 0), source->roleNames().key(role, -1));
}
int TaskFilter::value(int row, const char *role) const {
    return sourceValue(mapToSource(index(row, 0)).row(), role).toInt();
}
bool TaskFilter::belongs(int row) const {
    const auto appId = sourceValue(row, "appId").toString();
    if (!windowApp_.isEmpty() && appId != windowApp_)
        return false;
    if (!shell_)
        return app_.isEmpty();
    return shell_->pinnedAppFor(appId) == app_;
}
QString TaskFilter::groupOf(int row) const {
    return app_.isEmpty() ? sourceValue(row, "appId").toString() : app_;
}
bool TaskFilter::filterAcceptsRow(int row, const QModelIndex &parent) const {
    if (parent.isValid() || !belongs(row))
        return false;
    if (!grouped_)
        return true;
    const auto group = groupOf(row);
    if (group.isEmpty())
        return true;
    for (int earlier = 0; earlier < row; ++earlier)
        if (belongs(earlier) && groupOf(earlier) == group)
            return false;
    return true;
}
int TaskFilter::activeTask() const {
    for (int row = 0; row < rowCount(); ++row)
        if (value(row, "active"))
            return value(row, "taskId");
    return -1;
}
bool TaskFilter::minimized() const {
    for (int row = 0; row < rowCount(); ++row)
        if (!value(row, "minimized"))
            return false;
    return rowCount() > 0;
}
int TaskFilter::nextTask() const {
    if (rowCount() == 0)
        return -1;
    for (int row = 0; row < rowCount(); ++row)
        if (value(row, "active"))
            return value((row + 1) % rowCount(), "taskId");
    return value(0, "taskId");
}
void TaskFilter::move(int from, int to, int count) {
    if (count != 1 || from < 0 || to < 0 || from >= rowCount() || to >= rowCount() || from == to)
        return;
    auto *source = sourceModel();
    const int moved = mapToSource(index(from, 0)).row(), target = mapToSource(index(to, 0)).row();
    const auto group = groupOf(moved), targetGroup = groupOf(target);
    if (!grouped_ || (group.isEmpty() && targetGroup.isEmpty())) {
        QMetaObject::invokeMethod(source, "move", Q_ARG(int, moved), Q_ARG(int, target),
                                  Q_ARG(int, 1));
        return;
    }
    // The source rows in the order they should end up: the moved group's taken out and put
    // back beside the target's.
    auto member = [&](int row, int of, const QString &key) {
        return key.isEmpty() ? row == of : belongs(row) && groupOf(row) == key;
    };
    QList<int> order, rows;
    for (int row = 0; row < source->rowCount(); ++row)
        (member(row, moved, group) ? rows : order).push_back(row);
    int at = -1;
    for (int i = 0; i < order.size(); ++i)
        if (member(order[i], target, targetGroup)) {
            if (to < from) {
                at = i;
                break;
            }
            at = i + 1;
        }
    for (int i = 0; i < rows.size(); ++i)
        order.insert(at + i, rows[i]);
    // Applied one row at a time, tracking where each source row has got to.
    QList<int> current(source->rowCount());
    for (int i = 0; i < current.size(); ++i)
        current[i] = i;
    for (int i = 0; i < order.size(); ++i) {
        const int now = int(current.indexOf(order[i]));
        if (now == i)
            continue;
        QMetaObject::invokeMethod(source, "move", Q_ARG(int, now), Q_ARG(int, i), Q_ARG(int, 1));
        current.move(now, i);
    }
}
