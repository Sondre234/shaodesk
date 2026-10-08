// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class ShellController;

// The command palette (Super + P): one search box over open windows, installed applications,
// workspaces, compositor actions (the power actions among them), appearance profiles and saved
// sessions, with a calculator. Entries are {kind, title, subtitle, icon,
// target}; `kind` is window, app, workspace, action, session or calc, and a power action, which
// runs as the power menu runs it, also has `power` set. A calculation's target is its number,
// which running it copies to the clipboard.
class Palette : public QObject {
    Q_OBJECT
    // The output showing the palette, empty while it is closed.
    Q_PROPERTY(QString output READ output NOTIFY openChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY selectedChanged)
  public:
    explicit Palette(ShellController &controller);
    QString output() const { return output_; }
    QString query() const { return query_; }
    QVariantList results() const { return results_; }
    int selected() const { return selected_; }
    void setQuery(const QString &query);
    void setSelected(int selected);
    Q_INVOKABLE void open(const QString &output);
    Q_INVOKABLE void close();
    // Moves the selection, wrapping.
    Q_INVOKABLE void move(int delta);
    // Runs the result at `index` (the selected one when negative) and closes the palette.
    Q_INVOKABLE void activate(int index = -1);
    // What the palette searches but the applications, for a search of its own (the start
    // menu's): the windows of `windows`, a model with TaskModel's roles (the taskbar's, or a
    // stand-in for it), saved sessions, workspaces and actions.
    Q_INVOKABLE QVariantList entries(QObject *windows) const;
    // Runs one of its entries as choosing it here does, on monitor `output`.
    Q_INVOKABLE void run(const QVariantMap &entry, const QString &output);
    // The entry of the calculator's value for `query` (see calculator::answer), or an empty map
    // when it is no calculation.
    static QVariantMap calculation(const QString &query);
  Q_SIGNALS:
    void openChanged();
    void queryChanged();
    void resultsChanged();
    void selectedChanged();

  private:
    ShellController &controller_;
    QString output_, query_;
    QVariantList entries_, results_, sessions_;
    int selected_ = 0;
    void collect();
    void refreshResults();
};
