// SPDX-License-Identifier: GPL-3.0-or-later
// Applications' icons: the icon theme the desktop names, and which desktop entry a window
// belongs to by its app id.
#include "app_match.hpp"
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
        QVERIFY(
            write(config + "/kdeglobals", "[General]\nTheme=wrong\n[Icons]\nTheme=breeze-dark\n"));
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
    void trimsWhatProgramsAdd() {
        using app_match::trimmed;
        QCOMPARE(trimmed("gimp-2.10"), QString("gimp"));
        QCOMPARE(trimmed(".blueman-manager-wrapped"), QString("blueman-manager"));
        QCOMPARE(trimmed("..foo-wrapped-wrapped"), QString("foo"));
        QCOMPARE(trimmed("Firefox-bin"), QString("firefox"));
        QCOMPARE(trimmed("signal-desktop"), QString("signal"));
        QCOMPARE(trimmed("brave-browser-stable"), QString("brave"));
        QCOMPARE(trimmed("code-url-handler"), QString("code"));
        QCOMPARE(trimmed("Notepad++.exe"), QString("notepad++"));
        QCOMPARE(trimmed("Obsidian-1.6.7-x86_64.AppImage"), QString("obsidian"));
        QCOMPARE(trimmed("Minecraft 1.20.1"), QString("minecraft"));
        QCOMPARE(trimmed("org.gnome.Nautilus.desktop"), QString("org.gnome.nautilus"));
        // What is only a suffix or a number stays.
        QCOMPARE(trimmed("-bin"), QString("-bin"));
        QCOMPARE(trimmed("python3"), QString("python3"));
        QCOMPARE(trimmed("2048"), QString("2048"));
        QCOMPARE(trimmed(""), QString());
    }
    void foldsKeys() {
        using app_match::key;
        QCOMPARE(key("gnome-calculator"), key("GnomeCalculator"));
        QCOMPARE(key("gnome_calculator"), QString("gnomecalculator"));
        QCOMPARE(key("firefox-developer-edition"), QString("firefoxdeveloperedition"));
        QCOMPARE(key("TelegramDesktop"), QString("telegramdesktop"));
        QCOMPARE(key("Visual Studio Code"), QString("visualstudiocode"));
    }
    void findsTheProgram() {
        using app_match::program;
        QCOMPARE(program("gimp-2.10 %U"), QString("gimp-2.10"));
        QCOMPARE(program("/usr/lib/firefox/firefox-bin %u"), QString("firefox-bin"));
        QCOMPARE(program("\"/opt/My App/my-app\" --flag %F"), QString("my-app"));
        QCOMPARE(program("env GDK_BACKEND=x11 BAMF_DESKTOP_FILE_HINT=x /snap/bin/spotify %U"),
                 QString("spotify"));
        QCOMPARE(program("FOO=1 nice -n 10 gamemoderun prime-run heroic"), QString("heroic"));
        QCOMPARE(program("sh -c \"exec obs --startreplaybuffer\""), QString("obs"));
        QCOMPARE(
            program("/usr/bin/flatpak run --branch=stable --arch=x86_64 "
                    "--command=telegram-desktop --file-forwarding org.telegram.desktop @@u %u @@"),
            QString("telegram-desktop"));
        QCOMPARE(program("/usr/bin/flatpak run --branch=stable org.gimp.GIMP @@ %F @@"),
                 QString("org.gimp.GIMP"));
        QCOMPARE(program("steam steam://rungameid/570"), QString("steam"));
        QCOMPARE(program(""), QString());
        QCOMPARE(program("env"), QString());
    }
};

QTEST_GUILESS_MAIN(AppIconsTest)
#include "app_icons_test.moc"
