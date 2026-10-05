// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QAbstractListModel>
#include <QImage>
#include <QList>
#include <QString>
#include <vector>

// A status icon an application shows in the system tray (a StatusNotifierItem), as the panel
// draws it.
struct TrayItem {
    // The item's bus name and object path, which identify it.
    QString key;
    // Given by the model and never reused; names the item's pictures for the panel.
    int serial = 0;
    QString id, title;
    // "Passive" (not shown), "Active" or "NeedsAttention".
    QString status = "Active";
    QString iconName, attentionIconName, overlayIconName, iconThemePath;
    // The pictures sent instead of (or with) names, one per size.
    QList<QImage> icon, attentionIcon, overlayIcon;
    // The tooltip as plain text.
    QString toolTipTitle, toolTipText;
    // A click opens the menu rather than activating the application.
    bool itemIsMenu = false;
    // The menu's object path; empty without a menu.
    QString menuPath;
    // Bumped whenever what the icon shows changes, so that the panel loads it again.
    int revision = 0;
    // The title and text a tooltip shows, falling back on the item's title and id.
    QString toolTip() const;
};

// An IconPixmap entry, `width` x `height` ARGB32 pixels in network byte order; null when the
// sizes are out of range or `data` is too short.
QImage trayImageFromArgb32(int width, int height, const QByteArray &data);
// The pixmap whose size suits `size` best, the smallest at least that large or else the largest,
// scaled to it.
QImage trayPickPixmap(const QList<QImage> &pixmaps, QSize size);
// An icon an application keeps in a folder of its own (IconThemePath): NAME.png, .svg or .xpm
// there or in a theme laid out under it (hicolor/22x22/apps/NAME.png), the largest found; empty
// when there is none.
QString trayIconFile(const QString &name, const QString &themePath);

// The tray's items, in the order they registered. A host (TrayHost, over D-Bus) adds, updates
// and removes them, and carries out what the panel asks through the *Requested signals; without
// one the tray stays empty.
class TrayModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // How many items are not Passive: the panel hides the tray when there are none.
    Q_PROPERTY(int shown READ shown NOTIFY shownChanged)
  public:
    enum Role {
        KeyRole = Qt::UserRole + 1,
        TitleRole,
        StatusRole,
        ImageRole,
        ToolTipRole,
        ItemIsMenuRole,
        HasMenuRole
    };
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(items_.size()); }
    int shown() const { return shown_; }
    const std::vector<TrayItem> &items() const { return items_; }

    // Appends an item, giving it a serial; an item already there with the same key is replaced
    // in place.
    void add(TrayItem item);
    TrayItem *find(const QString &key);
    const TrayItem *find(int serial) const;
    // Tells views that find()'s item changed; `picture`: what its icon shows changed too.
    void changed(const QString &key, bool picture);
    void remove(const QString &key);
    void clear();
    // What item `serial`'s icon shows at `size`: the attention icon while it needs attention,
    // a named icon (in its theme path, by absolute path, or in the icon theme) before a pixmap,
    // and the overlay over the bottom right quarter. Null when it has no icon at all.
    QImage picture(int serial, QSize size) const;

    Q_INVOKABLE bool contains(const QString &key) const;
    // What the panel asks of an item, at a point on the screen.
    Q_INVOKABLE void activate(const QString &key, int x, int y) { Q_EMIT activateRequested(key, x, y); }
    Q_INVOKABLE void secondaryActivate(const QString &key, int x, int y) {
        Q_EMIT secondaryActivateRequested(key, x, y);
    }
    Q_INVOKABLE void contextMenu(const QString &key, int x, int y) { Q_EMIT contextMenuRequested(key, x, y); }
    // The wheel turned over the item by `delta` (120 a notch, as Qt's angleDelta).
    Q_INVOKABLE void scroll(const QString &key, int delta, bool horizontal) {
        Q_EMIT scrollRequested(key, delta, horizontal ? "horizontal" : "vertical");
    }

  Q_SIGNALS:
    void countChanged();
    void shownChanged();
    void activateRequested(const QString &key, int x, int y);
    void secondaryActivateRequested(const QString &key, int x, int y);
    void contextMenuRequested(const QString &key, int x, int y);
    void scrollRequested(const QString &key, int delta, const QString &orientation);
    // The item could not be activated (Ayatana's items have no Activate): the panel that asked
    // shows its menu instead.
    void activationRefused(const QString &key);

  private:
    std::vector<TrayItem> items_;
    int shown_ = 0, nextSerial_ = 1;
    int row(const QString &key) const;
    void updateShown();
};
