// SPDX-License-Identifier: GPL-3.0-or-later
#include "view.hpp"
#include "task_filter.hpp"
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QScreen>
#include <iostream>
#if SHAODESK_LAYER_SHELL
#include <LayerShellQt/Window>
#endif

ShellView::ShellView(ShellController &controller, QScreen *screen, bool desktop, bool preview)
    : QQuickView(controller.engine(), nullptr), controller_(controller), desktop_(desktop), preview_(preview), outputScreen_(screen) {
    setScreen(screen);
    setTitle(desktop ? "shaodesk desktop" : "shaodesk taskbar");
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);
    setFlags(Qt::FramelessWindowHint);
    static const int registered = qmlRegisterType<TaskFilter>("Shaodesk", 1, 0, "TaskFilter");
    Q_UNUSED(registered);
    // Known as soon as the window exists, so before any QML asks: every view of the shell draws
    // the same way.
    controller.setEffects(QSGRendererInterface::isApiRhiBased(rendererInterface()->graphicsApi()) &&
                          rendererInterface()->graphicsApi() != QSGRendererInterface::Null);
    // The engine is shared by every view; what differs per view goes in as initial properties.
    // outputName matches the compositor's output name, which the workspace state is keyed by.
    if (!desktop)
        setInitialProperties({{"shellView", QVariant::fromValue(this)}, {"outputName", screen->name()}});
#if SHAODESK_LAYER_SHELL
    if (!preview) {
        using W = LayerShellQt::Window;
        layer_ = W::get(this);
        layer_->setScreen(screen);
        layer_->setScope(desktop ? "shaodesk-desktop" : "shaodesk-panel");
        layer_->setLayer(desktop ? W::LayerBackground : W::LayerTop);
        placeLayer();
        layer_->setKeyboardInteractivity(W::KeyboardInteractivityNone);
        layer_->setActivateOnShow(false);
    }
#endif
    resizeForContent();
    setSource(QUrl(desktop ? "qrc:/shell/ShaodeskShell/Desktop.qml" : "qrc:/shell/ShaodeskShell/Panel.qml"));
    connect(&controller, &ShellController::configChanged, this, [this] {
        placeLayer();
        resizeForContent();
    });
    connect(&controller, &ShellController::launcherRequested, this, [this](const QString &output) {
        if (desktop_ || !rootObject() || outputScreen_->name() != output)
            return;
        bool open = !rootObject()->property("launcherOpen").toBool();
        rootObject()->setProperty("launcherOpen", open);
        std::cerr << "shaodesk launcher " << (open ? "opened" : "closed") << " on "
                  << output.toStdString() << '\n';
    });
    connect(&controller, &ShellController::powerMenuRequested, this, [this](const QString &output) {
        if (!desktop_ && rootObject() && outputScreen_->name() == output)
            QMetaObject::invokeMethod(rootObject(), "togglePowerMenu");
    });
    connect(screen, &QScreen::geometryChanged, this, [this] { resizeForContent(); });
    connect(this, &QWindow::activeChanged, this, [this] {
        if (!isActive() && expanded_ && rootObject())
            QMetaObject::invokeMethod(rootObject(), "closeMenus");
    });
}
// The panel's surface spans the output's width and the bar's margins; the bar is drawn inset.
void ShellView::placeLayer() {
#if SHAODESK_LAYER_SHELL
    if (!layer_)
        return;
    using W = LayerShellQt::Window;
    if (desktop_) {
        layer_->setAnchors(
            W::Anchors(W::AnchorLeft | W::AnchorRight | W::AnchorTop | W::AnchorBottom));
        layer_->setExclusiveZone(-1);
        return;
    }
    layer_->setAnchors(W::Anchors(W::AnchorLeft | W::AnchorRight |
                                  (controller_.panelTop() ? W::AnchorTop : W::AnchorBottom)));
    layer_->setExclusiveZone(controller_.panelExtent());
#endif
}
void ShellView::resizeForContent() {
    int width = preview_ ? 1100 : screen()->geometry().width();
    int height = desktop_ ? (preview_ ? 680 : screen()->geometry().height())
                          : (expanded_ ? std::min(560, screen()->geometry().height())
                                       : controller_.panelExtent());
    resize(width, height);
#if SHAODESK_LAYER_SHELL
    if (layer_)
        layer_->setDesiredSize(QSize(0, desktop_ ? 0 : height));
#endif
}
void ShellView::setExpanded(bool expanded, bool keyboard) {
    if (desktop_)
        return;
    expanded_ = expanded;
    keyboard = expanded && keyboard;
#if SHAODESK_LAYER_SHELL
    if (layer_)
        layer_->setKeyboardInteractivity(keyboard
                                             ? LayerShellQt::Window::KeyboardInteractivityExclusive
                                             : LayerShellQt::Window::KeyboardInteractivityNone);
#endif
    resizeForContent();
    if (keyboard)
        requestActivate();
}
OverviewView::OverviewView(ShellController &controller, QScreen *screen)
    : QQuickView(controller.engine(), nullptr), controller_(controller), outputScreen_(screen) {
    setScreen(screen);
    setTitle("shaodesk overview");
    setColor(Qt::transparent);
    setFlags(Qt::FramelessWindowHint);
    resize(screen->geometry().size());
    setInitialProperties({{"screenSize", screen->geometry().size()}});
#if SHAODESK_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_ = W::get(this);
    layer_->setScreen(screen);
    layer_->setScope("shaodesk-overview");
    layer_->setLayer(W::LayerOverlay);
    layer_->setAnchors(W::Anchors(W::AnchorTop | W::AnchorBottom | W::AnchorLeft | W::AnchorRight));
    layer_->setExclusiveZone(-1);
    layer_->setKeyboardInteractivity(W::KeyboardInteractivityNone);
    layer_->setActivateOnShow(false);
#endif
    setSource(QUrl("qrc:/shell/ShaodeskShell/Overview.qml"));
    connect(screen, &QScreen::geometryChanged, this, [this] {
        resize(outputScreen_->geometry().size());
        if (rootObject())
            rootObject()->setProperty("screenSize", outputScreen_->geometry().size());
    });
    connect(&controller, &ShellController::overviewChanged, this, &OverviewView::update);
}
void OverviewView::update() {
    const bool here = controller_.overviewOutput() == outputScreen_->name();
    if (here && !isVisible()) {
        show();
        std::cerr << "shaodesk overview shown on " << outputScreen_->name().toStdString() << '\n';
    } else if (!here && isVisible()) {
        hide();
        std::cerr << "shaodesk overview hidden on " << outputScreen_->name().toStdString() << '\n';
    }
}
PowerView::PowerView(ShellController &controller, QScreen *screen)
    : QQuickView(controller.engine(), nullptr), controller_(controller), outputScreen_(screen) {
    setScreen(screen);
    setTitle("shaodesk power");
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);
    setFlags(Qt::FramelessWindowHint);
    resize(screen->geometry().size());
#if SHAODESK_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_ = W::get(this);
    layer_->setScreen(screen);
    layer_->setScope("shaodesk-power");
    layer_->setLayer(W::LayerOverlay);
    layer_->setAnchors(W::Anchors(W::AnchorTop | W::AnchorBottom | W::AnchorLeft | W::AnchorRight));
    layer_->setExclusiveZone(-1);
    layer_->setKeyboardInteractivity(W::KeyboardInteractivityExclusive);
    layer_->setActivateOnShow(true);
#endif
    setSource(QUrl("qrc:/shell/ShaodeskShell/PowerDialog.qml"));
    connect(screen, &QScreen::geometryChanged, this,
            [this] { resize(outputScreen_->geometry().size()); });
    connect(controller.power(), &Power::pendingChanged, this, &PowerView::update);
}
void PowerView::update() {
    auto *power = controller_.power();
    const bool mine = !power->pending().isEmpty() && power->output() == outputScreen_->name();
    if (mine && !isVisible()) {
        show();
        requestActivate();
        if (rootObject())
            QMetaObject::invokeMethod(rootObject(), "reset");
        std::cerr << "shaodesk power dialog shown on " << outputScreen_->name().toStdString()
                  << '\n';
    } else if (!mine && isVisible()) {
        hide();
        std::cerr << "shaodesk power dialog hidden on " << outputScreen_->name().toStdString()
                  << '\n';
    }
}
SwitcherView::SwitcherView(ShellController &controller, QScreen *screen)
    : QQuickView(controller.engine(), nullptr), controller_(controller), outputScreen_(screen), delay_(new QTimer(this)) {
    setScreen(screen);
    setTitle("shaodesk switcher");
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeViewToRootObject);
    setFlags(Qt::FramelessWindowHint);
    setInitialProperties({{"screenSize", screen->geometry().size()}});
#if SHAODESK_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_ = W::get(this);
    layer_->setScreen(screen);
    layer_->setScope("shaodesk-switcher");
    layer_->setLayer(W::LayerOverlay);
    layer_->setAnchors(W::Anchors());
    layer_->setExclusiveZone(0);
    layer_->setKeyboardInteractivity(W::KeyboardInteractivityNone);
    layer_->setActivateOnShow(false);
    auto fit = [this] { layer_->setDesiredSize(size()); };
    connect(this, &QWindow::widthChanged, this, fit);
    connect(this, &QWindow::heightChanged, this, fit);
#endif
    setSource(QUrl("qrc:/shell/ShaodeskShell/Switcher.qml"));
    connect(screen, &QScreen::geometryChanged, this, [this] {
        if (rootObject())
            rootObject()->setProperty("screenSize", outputScreen_->geometry().size());
    });
    // A quick Alt+Tab switches without the overlay flashing up.
    delay_->setSingleShot(true);
    delay_->setInterval(120);
    connect(delay_, &QTimer::timeout, this, [this] {
        if (controller_.switcherOutput() == outputScreen_->name()) {
            show();
            std::cerr << "shaodesk switcher shown on " << outputScreen_->name().toStdString() << '\n';
        }
    });
    connect(&controller, &ShellController::switcherChanged, this, &SwitcherView::update);
}
void SwitcherView::update() {
    if (controller_.switcherOutput() != outputScreen_->name()) {
        delay_->stop();
        if (isVisible()) {
            hide();
            std::cerr << "shaodesk switcher hidden on " << outputScreen_->name().toStdString()
                      << '\n';
        }
    } else if (!isVisible() && !delay_->isActive()) {
        delay_->start();
    }
}
PaletteView::PaletteView(ShellController &controller, QScreen *screen)
    : QQuickView(controller.engine(), nullptr), controller_(controller), outputScreen_(screen) {
    setScreen(screen);
    setTitle("shaodesk palette");
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeViewToRootObject);
    setFlags(Qt::FramelessWindowHint);
    setInitialProperties({{"screenSize", screen->geometry().size()}});
#if SHAODESK_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_ = W::get(this);
    layer_->setScreen(screen);
    layer_->setScope("shaodesk-palette");
    layer_->setLayer(W::LayerOverlay);
    layer_->setAnchors(W::AnchorTop);
    layer_->setMargins(QMargins(0, screen->geometry().height() / 6, 0, 0));
    layer_->setExclusiveZone(0);
    layer_->setKeyboardInteractivity(W::KeyboardInteractivityExclusive);
    layer_->setActivateOnShow(true);
#endif
    setSource(QUrl("qrc:/shell/ShaodeskShell/Palette.qml"));
    // The surface is as big as the palette wants, whatever size the compositor last configured
    // (a palette that opened small would otherwise stay small).
    if (auto *root = rootObject()) {
        auto fit = [this, root] {
            const QSize wanted(qRound(root->width()), qRound(root->height()));
            if (size() != wanted)
                resize(wanted);
#if SHAODESK_LAYER_SHELL
            layer_->setDesiredSize(wanted);
#endif
        };
        connect(root, &QQuickItem::widthChanged, this, fit);
        connect(root, &QQuickItem::heightChanged, this, fit);
        connect(this, &QWindow::heightChanged, this, fit);
        fit();
    }
    connect(screen, &QScreen::geometryChanged, this, [this] {
        if (rootObject())
            rootObject()->setProperty("screenSize", outputScreen_->geometry().size());
#if SHAODESK_LAYER_SHELL
        if (layer_)
            layer_->setMargins(QMargins(0, outputScreen_->geometry().height() / 6, 0, 0));
#endif
    });
    // Clicking elsewhere takes the keyboard away, which closes the palette.
    connect(this, &QWindow::activeChanged, this, [this] {
        if (isActive())
            wasActive_ = true;
        else if (wasActive_ && isVisible())
            controller_.palette()->close();
    });
    connect(controller.palette(), &Palette::openChanged, this, &PaletteView::update);
}
void PaletteView::update() {
    const bool mine = controller_.palette()->output() == outputScreen_->name();
    if (mine && !isVisible()) {
        wasActive_ = false;
        if (rootObject())
            QMetaObject::invokeMethod(rootObject(), "reset");
        show();
        requestActivate();
        std::cerr << "shaodesk palette shown on " << outputScreen_->name().toStdString() << '\n';
    } else if (!mine && isVisible()) {
        hide();
        std::cerr << "shaodesk palette hidden on " << outputScreen_->name().toStdString() << '\n';
    }
}

namespace {
// Makes the surface as big as the item it shows, whatever size the compositor last configured
// (a surface that opened small would otherwise stay small).
void followRoot(QQuickView *view, LayerShellQt::Window *layer, QQuickItem *root) {
    auto fit = [view, layer, root] {
        const QSize wanted(qRound(root->width()), qRound(root->height()));
        if (view->size() != wanted)
            view->resize(wanted);
#if SHAODESK_LAYER_SHELL
        if (layer)
            layer->setDesiredSize(wanted);
#else
        Q_UNUSED(layer);
#endif
    };
    QObject::connect(root, &QQuickItem::widthChanged, view, fit);
    QObject::connect(root, &QQuickItem::heightChanged, view, fit);
    QObject::connect(view, &QWindow::heightChanged, view, fit);
    QObject::connect(view, &QWindow::widthChanged, view, fit);
    fit();
}
} // namespace
CardsView::CardsView(ShellController &controller, QScreen *screen)
    : QQuickView(controller.engine(), nullptr), controller_(controller), outputScreen_(screen) {
    setScreen(screen);
    setTitle("shaodesk notifications");
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeViewToRootObject);
    setFlags(Qt::FramelessWindowHint);
    setInitialProperties({{"outputName", screen->name()}});
#if SHAODESK_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_ = W::get(this);
    layer_->setScreen(screen);
    layer_->setScope("shaodesk-notifications");
    layer_->setLayer(W::LayerOverlay);
    layer_->setExclusiveZone(0);
    layer_->setKeyboardInteractivity(W::KeyboardInteractivityNone);
    layer_->setActivateOnShow(false);
    placeLayer();
#endif
    setSource(QUrl("qrc:/shell/ShaodeskShell/NotificationCards.qml"));
    if (auto *root = rootObject()) {
        connect(root, SIGNAL(activeChanged()), this, SLOT(update()));
        followRoot(this, layer_, root);
    }
    connect(&controller, &ShellController::configChanged, this, [this] { placeLayer(); });
    update();
}
void CardsView::placeLayer() {
#if SHAODESK_LAYER_SHELL
    using W = LayerShellQt::Window;
    W::Anchors anchors;
    anchors |= controller_.notifications()->bottom() ? W::AnchorBottom : W::AnchorTop;
    anchors |= controller_.notifications()->left() ? W::AnchorLeft : W::AnchorRight;
    layer_->setAnchors(anchors);
#endif
}
void CardsView::update() {
    const bool want = rootObject() && rootObject()->property("active").toBool();
    if (want && !isVisible()) {
        show();
        std::cerr << "shaodesk notifications shown on " << outputScreen_->name().toStdString() << '\n';
    } else if (!want && isVisible()) {
        hide();
        std::cerr << "shaodesk notifications hidden on " << outputScreen_->name().toStdString() << '\n';
    }
}
OsdView::OsdView(ShellController &controller, QScreen *screen)
    : QQuickView(controller.engine(), nullptr), controller_(controller), outputScreen_(screen) {
    setScreen(screen);
    setTitle("shaodesk osd");
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeViewToRootObject);
    setFlags(Qt::FramelessWindowHint | Qt::WindowTransparentForInput);
    setInitialProperties({{"outputName", screen->name()}});
#if SHAODESK_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_ = W::get(this);
    layer_->setScreen(screen);
    layer_->setScope("shaodesk-osd");
    layer_->setLayer(W::LayerOverlay);
    layer_->setExclusiveZone(-1);
    layer_->setKeyboardInteractivity(W::KeyboardInteractivityNone);
    layer_->setActivateOnShow(false);
    placeLayer();
#endif
    setSource(QUrl("qrc:/shell/ShaodeskShell/Osd.qml"));
    if (auto *root = rootObject()) {
        connect(root, SIGNAL(visibleNowChanged()), this, SLOT(update()));
        followRoot(this, layer_, root);
    }
    connect(&controller, &ShellController::configChanged, this, [this] { placeLayer(); });
}
void OsdView::placeLayer() {
#if SHAODESK_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_->setAnchors(controller_.osd()->top() ? W::AnchorTop : W::AnchorBottom);
    layer_->setMargins(QMargins(0, 48, 0, 48));
#endif
}
void OsdView::update() {
    const bool want = rootObject() && rootObject()->property("visibleNow").toBool();
    if (want && !isVisible()) {
        show();
        std::cerr << "shaodesk osd shown on " << outputScreen_->name().toStdString() << '\n';
    } else if (!want && isVisible()) {
        hide();
        std::cerr << "shaodesk osd hidden on " << outputScreen_->name().toStdString() << '\n';
    }
}
ConfigErrorView::ConfigErrorView(ShellController &controller, QScreen *screen)
    : QQuickView(controller.engine(), nullptr), outputScreen_(screen) {
    setScreen(screen);
    setTitle("shaodesk configuration error");
    setColor(Qt::transparent);
    setResizeMode(QQuickView::SizeRootObjectToView);
    setFlags(Qt::FramelessWindowHint | Qt::WindowTransparentForInput);
#if SHAODESK_LAYER_SHELL
    using W = LayerShellQt::Window;
    layer_ = W::get(this);
    layer_->setScreen(screen);
    layer_->setScope("shaodesk-config-error");
    layer_->setLayer(W::LayerOverlay);
    layer_->setAnchors(W::Anchors(W::AnchorTop) | W::AnchorLeft | W::AnchorRight);
    layer_->setExclusiveZone(-1);
    layer_->setKeyboardInteractivity(W::KeyboardInteractivityNone);
    layer_->setActivateOnShow(false);
#endif
    setSource(QUrl("qrc:/shell/ShaodeskShell/ConfigError.qml"));
    if (auto *root = rootObject()) {
        connect(root, SIGNAL(visibleNowChanged()), this, SLOT(update()));
        connect(root, SIGNAL(implicitHeightChanged()), this, SLOT(update()));
    }
    update();
}
void ConfigErrorView::update() {
    auto *root = rootObject();
    const bool want = root && root->property("visibleNow").toBool();
    if (root) {
        // The compositor stretches it across the output; only its height is asked for.
        const int height = qMax(1, qRound(root->implicitHeight()));
        resize(outputScreen_->geometry().width(), height);
#if SHAODESK_LAYER_SHELL
        layer_->setDesiredSize(QSize(0, height));
#endif
    }
    if (want && !isVisible()) {
        show();
        std::cerr << "shaodesk configuration error shown on " << outputScreen_->name().toStdString()
                  << '\n';
    } else if (!want && isVisible()) {
        hide();
    }
}
