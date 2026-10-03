// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QSocketNotifier>
#include <QTimer>

// The screen backlight (/sys/class/backlight): its level as a percentage, and a signal when it
// changes, for the on-screen display. The kernel sends a hotplug message each time a backlight's
// brightness changes, so with `watch` nothing polls; where that socket cannot be opened it
// polls once a second, and only where a backlight exists.
class Backlight : public QObject {
    Q_OBJECT
  public:
    // `pollMs` above 0 polls at that interval whatever else there is, for tests that write the
    // level themselves.
    explicit Backlight(QString root = "/sys", bool watch = false, int pollMs = 0, QObject *parent = nullptr);
    ~Backlight() override;
    bool present() const { return present_; }
    // 0 to 100; -1 without a backlight.
    int percent() const { return percent_; }
    bool watching() const { return notifier_ != nullptr; }
    // Whether a kernel hotplug message (key=value pairs separated by NULs) concerns a backlight.
    static bool relevantUevent(const QByteArray &message);
    // Reads the level again; emits changed() when it differs from the last read, but not for the
    // first read, which only establishes what is there.
    void refresh();
  Q_SIGNALS:
    void changed(int percent);

  private:
    QString root_;
    QSocketNotifier *notifier_ = nullptr;
    QTimer timer_;
    bool present_ = false, known_ = false;
    int percent_ = -1;
};
