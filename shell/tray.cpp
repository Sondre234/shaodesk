// SPDX-License-Identifier: GPL-3.0-or-later
#include "tray.hpp"
#include <algorithm>

QString TrayItem::toolTip() const {
    const QString head = !toolTipTitle.isEmpty() ? toolTipTitle : !title.isEmpty() ? title : id;
    return toolTipText.isEmpty() || toolTipText == head ? head : head + '\n' + toolTipText;
}

int TrayModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : count(); }
QVariant TrayModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= count())
        return {};
    const auto &item = items_[index.row()];
    switch (role) {
    case KeyRole: return item.key;
    case TitleRole: return !item.title.isEmpty() ? item.title : item.id;
    case StatusRole: return item.status;
    case ImageRole: return QString("image://tray/%1/%2").arg(item.serial).arg(item.revision);
    case ToolTipRole: return item.toolTip();
    case ItemIsMenuRole: return item.itemIsMenu;
    case HasMenuRole: return !item.menuPath.isEmpty();
    }
    return {};
}
QHash<int, QByteArray> TrayModel::roleNames() const {
    return {{KeyRole, "key"},     {TitleRole, "title"},           {StatusRole, "status"},
            {ImageRole, "image"}, {ToolTipRole, "toolTip"},       {ItemIsMenuRole, "itemIsMenu"},
            {HasMenuRole, "hasMenu"}};
}
int TrayModel::row(const QString &key) const {
    for (int i = 0; i < count(); ++i)
        if (items_[i].key == key)
            return i;
    return -1;
}
TrayItem *TrayModel::find(const QString &key) {
    const int i = row(key);
    return i < 0 ? nullptr : &items_[i];
}
const TrayItem *TrayModel::find(int serial) const {
    for (const auto &item : items_)
        if (item.serial == serial)
            return &item;
    return nullptr;
}
bool TrayModel::contains(const QString &key) const { return row(key) >= 0; }
void TrayModel::add(TrayItem item) {
    if (const int i = row(item.key); i >= 0) {
        item.serial = items_[i].serial;
        item.revision = items_[i].revision + 1;
        items_[i] = std::move(item);
        Q_EMIT dataChanged(index(i), index(i));
        updateShown();
        return;
    }
    item.serial = nextSerial_++;
    beginInsertRows({}, count(), count());
    items_.push_back(std::move(item));
    endInsertRows();
    Q_EMIT countChanged();
    updateShown();
}
void TrayModel::changed(const QString &key, bool picture) {
    const int i = row(key);
    if (i < 0)
        return;
    if (picture)
        ++items_[i].revision;
    Q_EMIT dataChanged(index(i), index(i));
    updateShown();
}
void TrayModel::remove(const QString &key) {
    const int i = row(key);
    if (i < 0)
        return;
    beginRemoveRows({}, i, i);
    items_.erase(items_.begin() + i);
    endRemoveRows();
    Q_EMIT countChanged();
    updateShown();
}
void TrayModel::clear() {
    if (items_.empty())
        return;
    beginResetModel();
    items_.clear();
    endResetModel();
    Q_EMIT countChanged();
    updateShown();
}
void TrayModel::updateShown() {
    const int shown = int(std::count_if(items_.begin(), items_.end(),
                                        [](const TrayItem &item) { return item.status != "Passive"; }));
    if (shown != shown_) {
        shown_ = shown;
        Q_EMIT shownChanged();
    }
}
