// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class ShellController;

// The power menu: the actions the compositor says may run now (from its "power" state line),
// and running one through the control socket. Entries are {action, title}.
class Power : public QObject {
    Q_OBJECT
    // The actions that may run, in menu order: "lock", "suspend", "hibernate".
    Q_PROPERTY(QStringList available READ available NOTIFY availableChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY availableChanged)
  public:
    explicit Power(ShellController &controller);
    QStringList available() const { return available_; }
    QVariantList entries() const;
    // What menus call an action: "Lock screen", "Suspend", ...; empty for one they do not offer.
    static QString title(const QString &action);
    // The compositor's list: "lock,suspend,logout", or "-" for none.
    void setAvailable(const QString &list);
    // Runs `action` if it may run.
    Q_INVOKABLE void request(const QString &action);
  Q_SIGNALS:
    void availableChanged();
    // The compositor would not run an action; `message` says why.
    void failed(const QString &message);

  private:
    ShellController &controller_;
    QStringList available_;
};
