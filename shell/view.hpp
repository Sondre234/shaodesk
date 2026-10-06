// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "controller.hpp"
#include <QQuickView>
#include <QTimer>

namespace LayerShellQt {
class Window;
}
class PopoverWindow;
class ShellView : public QQuickView {
    Q_OBJECT
  public:
    ShellView(ShellController &controller, QScreen *screen, bool desktop, bool preview);
    QScreen *outputScreen() const { return outputScreen_; }
    // Whether it is a layer surface; a preview is an ordinary window.
    bool layerShell() const { return layer_ != nullptr; }
    // The taskbar's popover, where its popups are drawn; nullptr for the desktop.
    PopoverWindow *popover() const;
    // The size of the output a preview stands for.
    static QSize previewSize() { return {1100, 720}; }

  private:
    ShellController &controller_;
    bool desktop_, preview_;
    LayerShellQt::Window *layer_ = nullptr;
    QScreen *outputScreen_;
    void placeLayer();
    void resizeForContent();
};

// The popups of one output's taskbar, in a surface of their own over the whole output: the bar's
// surface keeps its size whatever opens, and a popup can be as large as the output. Panel.qml
// declares it, so its popups stay in the panel's QML tree and state; its coordinates are the
// output's. With layer shell it is a layer surface above everything but the lock screen (above a
// fullscreen video too, for the launcher Super + R opens); without, an ordinary window as large
// as a preview's output.
//
// It is shown while `open`, holds the keyboard while `keyboard`, and takes the pointer only within
// `inputRects` (rectangles in its coordinates), so that the bar's surface beside the popups, and
// the windows beside a list shown on hover, stay reachable.
class PopoverWindow : public QQuickWindow {
    Q_OBJECT
    // The bar's view, whose output it covers. Set once, before it first opens.
    Q_PROPERTY(QQuickWindow *panel READ panel WRITE setPanel NOTIFY panelChanged)
    Q_PROPERTY(bool open READ open WRITE setOpen NOTIFY openChanged)
    Q_PROPERTY(bool keyboard READ keyboard WRITE setKeyboard NOTIFY keyboardChanged)
    Q_PROPERTY(QVariantList inputRects READ inputRects WRITE setInputRects NOTIFY inputRectsChanged)
  public:
    explicit PopoverWindow(QWindow *parent = nullptr);
    QQuickWindow *panel() const { return panel_; }
    void setPanel(QQuickWindow *panel);
    bool open() const { return open_; }
    void setOpen(bool open);
    bool keyboard() const { return keyboard_; }
    void setKeyboard(bool keyboard);
    QVariantList inputRects() const { return inputRects_; }
    void setInputRects(const QVariantList &rects);
    // Where it takes the pointer: inputRects as a region. Applied to the surface with layer shell
    // only, as a preview's platform may have no input regions.
    QRegion inputRegion() const { return inputRegion_; }
    // Shows it for a frame, empty and taking nothing, and hides it again: its first real opening
    // then finds the graphics set up (tens of milliseconds through the GPU) instead of waiting
    // for them. With layer shell only, and once.
    Q_INVOKABLE void prepare();

  Q_SIGNALS:
    void panelChanged();
    void openChanged();
    void keyboardChanged();
    void inputRectsChanged();
    // The keyboard went elsewhere while it held it, as when a window on another output is
    // clicked: the popups close.
    void dismissed();

  private:
    ShellView *panel_ = nullptr;
    bool open_ = false, keyboard_ = false, prepared_ = false;
    QVariantList inputRects_;
    QRegion inputRegion_;
    LayerShellQt::Window *layer_ = nullptr;
    void fit();
    void applyOpen();
    void applyKeyboard();
    void applyInput();
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

// The confirmation of power off, restart and log out on one output: a dimmed cover with the
// dialog in its middle, holding the keyboard while it waits.
class PowerView : public QQuickView {
    Q_OBJECT
  public:
    PowerView(ShellController &controller, QScreen *screen);
    QScreen *outputScreen() const { return outputScreen_; }

  private:
    ShellController &controller_;
    LayerShellQt::Window *layer_ = nullptr;
    QScreen *outputScreen_;
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
