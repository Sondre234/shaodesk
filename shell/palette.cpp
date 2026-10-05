// SPDX-License-Identifier: GPL-3.0-or-later
#include "palette.hpp"
#include "controller.hpp"
#include "fuzzy.hpp"

namespace {
struct Action {
    const char *name, *title;
};
// The compositor actions the palette offers, run as `shaodesk msg NAME`.
constexpr Action actions[] = {
    {"toggle_tiling", "Toggle tiling"},
    {"layout_next", "Next tiling layout"},
    {"layout_dwindle", "Layout: dwindle"},
    {"layout_master", "Layout: master and stack"},
    {"layout_spiral", "Layout: spiral"},
    {"layout_monocle", "Layout: monocle"},
    {"layout_scroll", "Layout: scrolling columns"},
    {"toggle_floating", "Toggle floating"},
    {"fullscreen", "Toggle fullscreen"},
    {"maximize", "Maximize window"},
    {"restore", "Restore window"},
    {"snap_left", "Snap window left"},
    {"snap_right", "Snap window right"},
    {"close", "Close window"},
    {"promote", "Promote window to master"},
    {"focus_last", "Focus the previous window"},
    {"focus_urgent", "Focus the window asking for attention"},
    {"toggle_sticky", "Toggle sticky window"},
    {"move_to_scratchpad", "Hide window in the scratchpad"},
    {"scratchpad_show", "Show the scratchpad"},
    {"group_toggle", "Group: make a tab group, or dissolve it"},
    {"group_next", "Group: next tab"},
    {"group_prev", "Group: previous tab"},
    {"ungroup", "Group: take the window out"},
    {"swallow_toggle", "Swallow the terminal this window came from, or let it out"},
    {"toggle_overview", "Overview of windows"},
    {"peek_toggle", "Peek at the desktop"},
    {"workspace_back", "Back to the previous workspace"},
    {"launcher", "Applications menu"},
    {"reload", "Reload configuration"},
};

QVariantMap entry(const QString &kind, const QString &title, const QString &subtitle,
                  const QString &icon, const QString &target, int number = 0, bool urgent = false) {
    return {{"kind", kind}, {"title", title}, {"subtitle", subtitle}, {"icon", icon},
            {"target", target}, {"number", number}, {"urgent", urgent}};
}

} // namespace

Palette::Palette(ShellController &controller) : QObject(&controller), controller_(controller) {}

void Palette::open(const QString &output) {
    if (output.isEmpty())
        return;
    output_ = output;
    query_.clear();
    sessions_.clear();
    collect();
    selected_ = 0;
    refreshResults(); // before the view shows, so it opens at its full size
    Q_EMIT queryChanged();
    Q_EMIT openChanged();
    // Saved sessions come from the compositor; they join the list when it answers.
    controller_.ask("session list\n", [this](const QByteArray &reply) {
        QVariantList sessions;
        const auto lines = reply.split('\n');
        if (lines.isEmpty() || lines[0] != "ok")
            return;
        for (int i = 1; i < lines.size(); ++i) {
            const auto fields = lines[i].split('\t');
            if (fields.size() != 3)
                continue;
            const auto name = QString::fromUtf8(fields[0]);
            const auto count = fields[1].toInt();
            sessions.push_back(QVariantMap{{"name", name}, {"windows", count}});
        }
        sessions_ = sessions;
        if (!output_.isEmpty()) {
            // Someone who has already moved down the list keeps the entry they are on.
            const auto before = selected_ > 0 && selected_ < results_.size()
                                    ? results_[selected_].toMap()
                                    : QVariantMap{};
            collect();
            refreshResults();
            for (int i = 0; !before.isEmpty() && i < results_.size(); ++i) {
                const auto item = results_[i].toMap();
                if (item["kind"] == before["kind"] && item["target"] == before["target"]) {
                    selected_ = i;
                    Q_EMIT selectedChanged();
                    break;
                }
            }
        }
    });
}

void Palette::close() {
    if (output_.isEmpty())
        return;
    output_.clear();
    Q_EMIT openChanged();
}

void Palette::collect() {
    QVariantList entries;
    auto *tasks = controller_.tasks();
    // Windows asking for attention come first, so an empty query offers them at the top.
    QVariantList urgent, others;
    for (int row = 0; row < tasks->rowCount(); ++row) {
        const auto index = tasks->index(row);
        const auto appId = tasks->data(index, TaskModel::AppId).toString();
        const bool asking = tasks->data(index, TaskModel::Urgent).toBool();
        const auto detail = appId.isEmpty() ? QString("Window") : "Window · " + appId;
        (asking ? urgent : others)
            .push_back(entry("window", tasks->data(index, TaskModel::Title).toString(),
                             asking ? detail + " · needs attention" : detail,
                             controller_.iconFor(appId),
                             QString::number(tasks->data(index, TaskModel::TaskId).toInt()),
                             tasks->data(index, TaskModel::Active).toBool() ? 1 : 0, asking));
    }
    entries += urgent;
    entries += others;
    for (const auto &item : sessions_) {
        const auto map = item.toMap();
        const auto name = map["name"].toString();
        const auto count = map["windows"].toInt();
        const auto detail = QString("Session · %1 window%2").arg(count).arg(count == 1 ? "" : "s");
        entries.push_back(entry("session", "Restore session " + name, detail, "document-open",
                                "session restore " + name));
        entries.push_back(entry("session", "Restore session " + name + " and launch missing apps",
                                detail, "document-open", "session restore " + name + " launch"));
    }
    for (int number = 1; number <= controller_.workspaceCount(); ++number) {
        const auto names = controller_.workspaceNames();
        const auto label = names.value(number - 1);
        const auto title = label.isEmpty() ? QString("Workspace %1").arg(number)
                                           : QString("Workspace %1: %2").arg(number).arg(label);
        entries.push_back(entry("workspace", title, "Switch workspace", "user-desktop",
                                QString::number(number), number));
        entries.push_back(entry("workspace", "Move window to " + title.toLower(),
                                "Move the focused window", "go-jump",
                                "move_to_workspace " + QString::number(number)));
    }
    for (const auto &name : controller_.profiles())
        entries.push_back(entry("action", "Appearance: " + name,
                                name == controller_.profile() ? "Profile · in use" : "Profile",
                                "preferences-desktop-theme", "profile " + name));
    for (const auto &action : actions)
        entries.push_back(entry("action", action.title, QString("Action · ") + action.name,
                                "system-run", action.name));
    // The power actions that may run; power off, restart and log out ask first, as from the
    // panel.
    static const QMap<QString, QString> powerIcons{
        {"lock", "system-lock-screen"}, {"suspend", "system-suspend"},
        {"hibernate", "system-suspend-hibernate"}, {"reboot", "system-reboot"},
        {"poweroff", "system-shutdown"}, {"logout", "system-log-out"}};
    for (const auto &action : controller_.power()->available()) {
        auto item = entry("action", Power::title(action), "Action · " + action,
                          powerIcons.value(action, "system-run"), action);
        item["power"] = true;
        entries.push_back(item);
    }
    for (const auto &app : controller_.apps()) {
        const auto map = app.toMap();
        entries.push_back(entry("app", map["name"].toString(), "Application",
                                map["icon"].toString(), map["appId"].toString()));
    }
    entries_ = entries;
}

void Palette::refreshResults() {
    results_ = fuzzy::rank(entries_, query_);
    // Saving the arrangement under the name typed is offered last, once the text can be a name.
    const auto typed = query_.trimmed();
    if (fuzzy::validSessionName(typed) && !typed.startsWith('>') && !typed.startsWith('@') &&
        !typed.startsWith('#') && !typed.startsWith('%'))
        results_.push_back(entry("session", "Save session as " + typed,
                                 "Session · keep this arrangement of windows", "document-save",
                                 "session save " + typed));
    selected_ = 0;
    Q_EMIT resultsChanged();
    Q_EMIT selectedChanged();
}

void Palette::setQuery(const QString &query) {
    if (query == query_)
        return;
    query_ = query;
    Q_EMIT queryChanged();
    refreshResults();
}

void Palette::setSelected(int selected) {
    if (results_.isEmpty() || selected < 0 || selected >= results_.size() || selected == selected_)
        return;
    selected_ = selected;
    Q_EMIT selectedChanged();
}

void Palette::move(int delta) {
    if (results_.isEmpty())
        return;
    selected_ = ((selected_ + delta) % results_.size() + results_.size()) % results_.size();
    Q_EMIT selectedChanged();
}

void Palette::activate(int index) {
    if (index < 0)
        index = selected_;
    if (index < 0 || index >= results_.size())
        return;
    const auto item = results_[index].toMap();
    const auto kind = item["kind"].toString();
    const auto target = item["target"].toString();
    const auto output = output_;
    close(); // hand the keyboard back before the request takes effect
    if (kind == "window") {
        if (item["number"].toInt() == 0) // the focused one would minimize on a second click
            controller_.tasks()->activate(target.toInt());
    } else if (item["power"].toBool()) {
        controller_.power()->request(target, output);
    } else if (kind == "app") {
        controller_.launch(target);
    } else if (kind == "workspace" && target.toInt() > 0) {
        controller_.showWorkspace(output, target.toInt());
    } else if (kind == "workspace" || kind == "action" || kind == "session") {
        controller_.send(target);
    }
}
