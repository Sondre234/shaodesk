// SPDX-License-Identifier: GPL-3.0-or-later
#include "icons.hpp"
#include <QIcon>
#include <QPainter>

QImage Icons::requestImage(const QString &id, QSize *size, const QSize &requested) {
    QSize dimensions = requested.isEmpty() ? QSize(48, 48) : requested;
    QIcon icon = id.startsWith('/') ? QIcon(id) : QIcon::fromTheme(id);
    if (icon.isNull())
        icon = QIcon::fromTheme("application-x-executable");
    QImage image;
    if (!icon.isNull())
        image = icon.pixmap(dimensions).toImage();
    if (image.isNull()) {
        image = QImage(dimensions, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#8aaff4"));
        painter.drawRoundedRect(image.rect().adjusted(4, 4, -4, -4), 8, 8);
        painter.setPen(QColor("#172237"));
        QFont font = painter.font();
        font.setPixelSize(dimensions.height() / 2);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(image.rect(), Qt::AlignCenter, "+");
    }
    if (size)
        *size = image.size();
    return image;
}
