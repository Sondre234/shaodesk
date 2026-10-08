// SPDX-License-Identifier: GPL-3.0-or-later
// Applications' icons: the icon theme the desktop names.
#include "icons.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>
#include <gio/gio.h>

namespace {
bool write(const QString &path, const QByteArray &text) {
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
}
bool gnomeSchema() {
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema =
        source ? g_settings_schema_source_lookup(source, "org.gnome.desktop.interface", TRUE)
               : nullptr;
    if (schema)
        g_settings_schema_unref(schema);
    return schema;
}
} // namespace

class AppIconsTest : public QObject {
    Q_OBJECT
    QTemporaryDir home;
  private Q_SLOTS:
    void initTestCase() {
        QVERIFY(home.isValid());
        qputenv("XDG_CONFIG_HOME", home.filePath("config").toLocal8Bit());
        // Settings that last only while the test runs, unset to begin with.
        qputenv("GSETTINGS_BACKEND", "memory");
    }
    void themeFromTheDesktopsSettings() {
        const auto config = home.filePath("config");
        QCOMPARE(desktopIconTheme(), QString());
        QVERIFY(write(config + "/kdeglobals", "[General]\nTheme=wrong\n[Icons]\nTheme=breeze-dark\n"));
        QCOMPARE(desktopIconTheme(), QString("breeze-dark"));
        QVERIFY(write(config + "/gtk-3.0/settings.ini",
                      "[Settings]\ngtk-theme-name=Adwaita\ngtk-icon-theme-name = Papirus\n"));
        QCOMPARE(desktopIconTheme(), QString("Papirus"));
        // GTK 4's settings come before GTK 3's.
        QVERIFY(write(config + "/gtk-4.0/settings.ini", "[Settings]\ngtk-icon-theme-name=Tela\n"));
        QCOMPARE(desktopIconTheme(), QString("Tela"));
        if (!gnomeSchema())
            QSKIP("GNOME's desktop schemas are not installed");
        // GNOME's setting comes first, but only once it is set: its default says nothing.
        GSettings *settings = g_settings_new("org.gnome.desktop.interface");
        QCOMPARE(desktopIconTheme(), QString("Tela"));
        g_settings_set_string(settings, "icon-theme", "Yaru");
        QCOMPARE(desktopIconTheme(), QString("Yaru"));
        g_settings_reset(settings, "icon-theme");
        g_object_unref(settings);
        QCOMPARE(desktopIconTheme(), QString("Tela"));
    }
};

QTEST_GUILESS_MAIN(AppIconsTest)
#include "app_icons_test.moc"
