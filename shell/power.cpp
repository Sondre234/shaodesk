// SPDX-License-Identifier: GPL-3.0-or-later
#include "power.hpp"
#include "controller.hpp"

namespace {
struct Action {
    const char *name, *title;
};
// The actions the menu offers, in its order.
constexpr Action actions[] = {
    {"lock", "Lock screen"},
    {"suspend", "Suspend"},
    {"hibernate", "Hibernate"},
};
} // namespace

Power::Power(ShellController &controller) : QObject(&controller), controller_(controller) {}

QString Power::title(const QString &action) {
    for (const auto &entry : actions)
        if (action == entry.name)
            return entry.title;
    return {};
}

QVariantList Power::entries() const {
    QVariantList list;
    for (const auto &action : available_)
        list.push_back(QVariantMap{{"action", action}, {"title", title(action)}});
    return list;
}

void Power::setAvailable(const QString &list) {
    QStringList offered;
    for (const auto &action : list.split(',', Qt::SkipEmptyParts))
        if (!title(action).isEmpty())
            offered.push_back(action);
    if (offered == available_)
        return;
    available_ = offered;
    Q_EMIT availableChanged();
}

void Power::request(const QString &action) {
    if (!available_.contains(action))
        return;
    controller_.ask((action + "\n").toUtf8(), [this, action](const QByteArray &reply) {
        if (reply.startsWith("ok"))
            return;
        auto message = QString::fromUtf8(reply).trimmed();
        if (message.startsWith("error: "))
            message = message.sliced(7);
        Q_EMIT failed(title(action) + ": " + (message.isEmpty() ? "no reply" : message));
    });
}
