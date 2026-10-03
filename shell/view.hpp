// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "controller.hpp"
#include <QQuickView>
#include <QTimer>

namespace LayerShellQt {
class Window;
}
class ShellView : public QQuickView {
    Q_OBJECT
  public:
    ShellView(ShellController &controller, QScreen *screen, bool desktop, bool preview);
    // Grows the surface to make room for a popup; `keyboard` takes the keyboard for it too, which
    // a preview shown on hover must not.
    Q_INVOKABLE void setExpanded(bool expanded, bool keyboard = true);
    QScreen *outputScreen() const { return outputScreen_; }

  private:
    ShellController &controller_;
    bool desktop_, preview_, expanded_ = false;
    LayerShellQt::Window *layer_ = nullptr;
    QScreen *outputScreen_;
    void placeLayer();
    void resizeForContent();
};

// The window switcher's overlay on one output, shown in the middle of it while the compositor's
// switcher is open there.
class SwitcherView : public QQuickView {
    Q_OBJECT
  public:
    SwitcherView(ShellController &controller, QScreen *screen);
    QScreen *outputScreen() const { return outputScreen_; }

  private:
    ShellController &controller_;
    LayerShellQt::Window *layer_ = nullptr;
    QScreen *outputScreen_;
    QTimer *delay_;
    void update();
};
