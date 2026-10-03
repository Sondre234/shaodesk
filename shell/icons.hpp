// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QQuickImageProvider>

// image://icons/NAME: a theme icon by name, or an image file by absolute path, with a drawn
// stand-in when neither exists.
class Icons : public QQuickImageProvider {
  public:
    Icons() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;
};
