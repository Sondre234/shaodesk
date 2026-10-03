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

// The command palette's overlay on one output: a search box near the top, holding the keyboard
// while the palette is open there.
class PaletteView : public QQuickView {
    Q_OBJECT
  public:
    PaletteView(ShellController &controller, QScreen *screen);
    QScreen *outputScreen() const { return outputScreen_; }

  private:
    ShellController &controller_;
    LayerShellQt::Window *layer_ = nullptr;
    QScreen *outputScreen_;
    bool wasActive_ = false;
    void update();
};

// The overview's overlay on one output: the text over the compositor's thumbnails (titles,
// workspace labels, the search box), covering the output while the overview is open there.
class OverviewView : public QQuickView {
    Q_OBJECT
  public:
    OverviewView(ShellController &controller, QScreen *screen);
    QScreen *outputScreen() const { return outputScreen_; }

  private:
    ShellController &controller_;
    LayerShellQt::Window *layer_ = nullptr;
    QScreen *outputScreen_;
    void update();
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

// The notification cards on one output: a stack from a corner, on screen while it is the output
// the controller chose for them and until the last card has slid away.
class CardsView : public QQuickView {
    Q_OBJECT
  public:
    CardsView(ShellController &controller, QScreen *screen);
    QScreen *outputScreen() const { return outputScreen_; }

  private Q_SLOTS:
    void update();

  private:
    ShellController &controller_;
    LayerShellQt::Window *layer_ = nullptr;
    QScreen *outputScreen_;
    void placeLayer();
};

// The on-screen display on one output: a pill near an edge, for the moment it is shown.
class OsdView : public QQuickView {
    Q_OBJECT
  public:
    OsdView(ShellController &controller, QScreen *screen);
    QScreen *outputScreen() const { return outputScreen_; }

  private Q_SLOTS:
    void update();

  private:
    ShellController &controller_;
    LayerShellQt::Window *layer_ = nullptr;
    QScreen *outputScreen_;
    void placeLayer();
};

// The configuration error banner across the top of one output, shown while the default
// configuration stands in for one with an error. Clicks pass through it.
class ConfigErrorView : public QQuickView {
    Q_OBJECT
  public:
    ConfigErrorView(ShellController &controller, QScreen *screen);
    QScreen *outputScreen() const { return outputScreen_; }

  private Q_SLOTS:
    void update();

  private:
    LayerShellQt::Window *layer_ = nullptr;
    QScreen *outputScreen_;
};
