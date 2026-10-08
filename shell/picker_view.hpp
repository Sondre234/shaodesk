// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "view.hpp"

// The overlay of a picker on one output, the clipboard history's: a card near the top, as the
// command palette's, holding the keyboard while its model is open there. The model has an `output`
// property (the output it is open on, empty while closed), an openChanged() signal and close();
// the QML root has `screenSize`, `shown`, `progress` and reset(), as Palette.qml has.
class PickerView : public OverlayView {
    Q_OBJECT
  public:
    // `name` is how its log lines and its layer surface's namespace call it ("clipboard" in
    // "shaodesk clipboard shown on DP-1" and "shaodesk-clipboard"); `file` its QML.
    PickerView(ShellController &controller, QScreen *screen, const char *name, const QString &file,
               QObject *model);

  private Q_SLOTS:
    // Shows it as its model opens on this output, and starts it going as it closes.
    void update();

  private:
    QObject *model_;
    bool wasActive_ = false;
    void place();
};
