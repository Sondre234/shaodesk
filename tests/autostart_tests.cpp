// SPDX-License-Identifier: GPL-3.0-or-later
// XDG autostart: splitting Exec, which entries are started and why the others are not, which
// directory's file counts, and the entries for what the shell provides itself.
#include "shaodesk/autostart.hpp"
#include "shaodesk/config.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

namespace fs = std::filesystem;
using Words = std::vector<std::string>;

static void require(bool condition, const std::string &message) {
    if (!condition)
        throw std::runtime_error(message);
}
static std::string show(const std::optional<Words> &words) {
    if (!words)
        return "(nothing)";
    std::string text;
    for (const auto &word : *words)
        text += "[" + word + "]";
    return text;
}
static void splits(const std::string &exec, const Words &expected) {
    auto words = shaodesk::parse_exec(exec, "Name", "icon-name", "/x/a.desktop");
    require(words && *words == expected, "Exec=" + exec + " gave " + show(words));
}
static void broken(const std::string &exec) {
    auto words = shaodesk::parse_exec(exec);
    require(!words, "Exec=" + exec + " should not split, but gave " + show(words));
}
static void rejects(const std::string &source) {
    try {
        (void)shaodesk::parse_config(source);
    } catch (const std::exception &) {
        return;
    }
    throw std::runtime_error("invalid configuration was accepted: " + source);
}
static void write(const fs::path &path, const std::string &text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

int main() {
    char pattern[] = "/tmp/autostart-XXXXXX";
    if (!mkdtemp(pattern)) {
        std::cerr << "cannot make a temporary directory\n";
        return 1;
    }
    const fs::path root = pattern;
    try {
        // Exec: blanks separate arguments; double quotes, with \ before " ` $ \; single quotes
        // and backslashes as a shell takes them; the file's own escapes come first.
        splits("foo", {"foo"});
        splits("  foo   --bar  baz ", {"foo", "--bar", "baz"});
        splits(R"(sh -c "echo \"hi there\" \$HOME")", {"sh", "-c", "echo \"hi there\" $HOME"});
        splits(R"(sh -c "a \\\\ b")", {"sh", "-c", "a \\ b"});
        splits("sh -c 'sleep 1; foo'", {"sh", "-c", "sleep 1; foo"});
        splits(R"(foo a\ b)", {"foo", "a b"});
        splits(R"(foo\sbar)", {"foo", "bar"});
        splits(R"(foo "")", {"foo", ""});
        // Field codes: files and URLs go, %i %c %k expand, %% is a %.
        splits("foo %U", {"foo"});
        splits("foo %f --x", {"foo", "--x"});
        splits("foo --file=%f", {"foo", "--file="});
        splits("foo %i %c %k", {"foo", "--icon", "icon-name", "Name", "/x/a.desktop"});
        splits("foo 100%%", {"foo", "100%"});
        splits("foo %d %D %n %N %v %m %z", {"foo"});
        require(shaodesk::parse_exec("foo %i") == Words{"foo"}, "%i without an icon");
        broken("");
        broken("   ");
        broken(R"(foo "bar)");
        broken("foo 'bar");
        broken("%U");

        // What the shell provides itself, by program, TryExec or file name.
        auto role = [](const std::string &name, const Words &command,
                       const std::string &try_exec = {}) {
            return shaodesk::autostart_role(name, command, try_exec);
        };
        require(role("dunst.desktop", {"dunst"}) == "notifications", "dunst");
        require(role("x.desktop", {"/usr/bin/mako", "--config", "f"}) == "notifications", "mako");
        require(role("xfce4-notifyd.desktop", {"/usr/lib/xfce4/notifyd/xfce4-notifyd"}) ==
                    "notifications",
                "xfce4-notifyd");
        require(role("notification-daemon.desktop",
                     {"/usr/lib/notification-daemon-1.0/notification-daemon"}) == "notifications",
                "notification-daemon");
        require(role("y.desktop", {"env", "-u", "X", "A=b", "swaync"}) == "notifications",
                "swaync through env");
        require(role("snixembed.desktop", {"snixembed", "--fork"}) == "tray", "snixembed");
        require(role("xembedsniproxy.desktop", {"xembedsniproxy"}).empty(),
                "xembedsniproxy is no watcher");
        require(role("polkit-gnome-authentication-agent-1.desktop",
                     {"/usr/libexec/polkit-gnome-authentication-agent-1"}) == "polkit",
                "polkit-gnome");
        require(role("org.kde.polkit-kde-authentication-agent-1.desktop", {"/usr/lib/x"}) ==
                    "polkit",
                "polkit-kde by its file name");
        require(role("lxqt-policykit-agent.desktop", {"lxqt-policykit-agent"}) == "polkit",
                "lxqt-policykit-agent");
        require(role("a.desktop", {"sh"}, "/usr/bin/hyprpolkitagent") == "polkit",
                "by TryExec");
        require(role("soteria.desktop", {"soteria"}) == "polkit", "soteria");
        require(role("firefox.desktop", {"firefox"}).empty(), "firefox is none of them");
        require(role("nm-applet.desktop", {"nm-applet"}).empty(), "nm-applet is none of them");

        // Directories: the user's hides the system's of the same name.
        auto home = root / "home", system = root / "etc", other = root / "other";
        write(home / "both.desktop", "[Desktop Entry]\nType=Application\nExec=mine\n");
        write(system / "both.desktop", "[Desktop Entry]\nType=Application\nExec=theirs\n");
        write(home / "off.desktop", "[Desktop Entry]\nHidden=true\n");
        write(system / "off.desktop", "[Desktop Entry]\nType=Application\nExec=system-thing\n");
        write(other / "off.desktop", "[Desktop Entry]\nType=Application\nExec=other-thing\n");
        write(other / "z.desktop", "[Desktop Entry]\nType=Application\nExec=z\n");
        write(system / "notes.txt", "[Desktop Entry]\nExec=not-desktop\n");
        write(system / ".hidden.desktop", "[Desktop Entry]\nExec=dot\n");
        fs::create_directories(system / "dir.desktop");
        shaodesk::AutostartOptions plain;
        plain.desktops = {"shaodesk"};
        auto entries = shaodesk::autostart_entries({home, system, other, root / "missing"}, plain);
        require(entries.size() == 3, "three entries, by name: " + std::to_string(entries.size()));
        require(entries[0].name == "both.desktop" && entries[0].path == home / "both.desktop" &&
                    entries[0].command == Words{"mine"} && entries[0].skip.empty(),
                "the user's entry wins");
        require(entries[1].name == "off.desktop" && entries[1].skip == "Hidden=true" &&
                    entries[1].path == home / "off.desktop",
                "the user's Hidden=true turns the system's entry off: " + entries[1].skip);
        require(entries[2].name == "z.desktop" && entries[2].skip.empty(), "z");

        // Each reason an entry is skipped, in the order they are checked.
        auto check = [&](const std::string &text, const std::string &skip,
                         const shaodesk::AutostartOptions &options) {
            auto path = root / "one" / "entry.desktop";
            write(path, text);
            auto entry = shaodesk::read_autostart_entry(path, "entry.desktop", options);
            require(entry.skip == skip, "expected skip '" + skip + "', got '" + entry.skip +
                                            "' for:\n" + text);
            return entry;
        };
        auto start = [&](const std::string &text) { return check(text, "", plain); };
        auto entry = start("# comment\n[Desktop Entry]\nName = My App\nName[de]=Meine\n"
                           "Exec = app --x %U\nPath=/srv/work\n\n[Desktop Action new]\n"
                           "Exec=other\n");
        require(entry.command == Words{"app", "--x"} && entry.directory == "/srv/work" &&
                    entry.program_name == "My App",
                "keys of [Desktop Entry] alone, spaces around = ignored");
        start("[Desktop Entry]\nExec=app\n"); // Type left out
        start("[Desktop Entry]\nType=Application\nExec=app\nOnlyShowIn=GNOME;shaodesk;\n");
        start("[Desktop Entry]\nType=Application\nExec=app\nOnlyShowIn=SHAODESK\n");
        start("[Desktop Entry]\nType=Application\nExec=app\nNotShownIn=GNOME;KDE;\n");
        start("[Desktop Entry]\nType=Application\nExec=app\nX-GNOME-Autostart-enabled=true\n");
        start("[Desktop Entry]\nType=Application\nExec=app\nTryExec=/bin/sh\n");
        start("[Desktop Entry]\nType=Application\nExec=app\nTryExec=sh\n");
        start("[Desktop Entry]\nType=Application\nExec=app\nHidden=false\nTerminal=false\n");
        check("[Desktop Entry]\nType=Application\nExec=app\n", "excluded by autostart.exclude",
              shaodesk::AutostartOptions{{"entry.desktop"}, {"shaodesk"}});
        check("[Desktop Entry]\nType=Application\nExec=app\nHidden=true\n", "Hidden=true", plain);
        check("[Desktop Entry]\nType=Link\nURL=https://example.org\n", "Type=Link, not Application",
              plain);
        check("[Desktop Entry]\nType=Application\nExec=app\nOnlyShowIn=GNOME;KDE;\n",
              "OnlyShowIn=GNOME;KDE;", plain);
        check("[Desktop Entry]\nType=Application\nExec=app\nOnlyShowIn=GNOME;\n",
              "OnlyShowIn=GNOME;", shaodesk::AutostartOptions{});
        check("[Desktop Entry]\nType=Application\nExec=app\nNotShownIn=KDE;shaodesk;\n",
              "NotShownIn=KDE;shaodesk;", plain);
        check("[Desktop Entry]\nType=Application\nExec=app\nX-GNOME-Autostart-enabled=false\n",
              "X-GNOME-Autostart-enabled=false", plain);
        check("[Desktop Entry]\nType=Application\nExec=app\nTryExec=no-such-program-here\n",
              "TryExec no-such-program-here is not installed", plain);
        check("[Desktop Entry]\nType=Application\nExec=app\nTryExec=/nonexistent/app\n",
              "TryExec /nonexistent/app is not installed", plain);
        check("[Desktop Entry]\nType=Application\nName=x\n", "no Exec", plain);
        check("[Desktop Entry]\nType=Application\nExec=app \"open\n",
              "Exec cannot be split into arguments: app \"open", plain);
        check("[Desktop Entry]\nType=Application\nExec=htop\nTerminal=true\n",
              "Terminal=true: shaodesk does not open a terminal for it", plain);
        check("Exec=app\n",
              "not a desktop entry: no [Desktop Entry] group, or it cannot be read", plain);
        check("[Desktop Action x]\nExec=app\n",
              "not a desktop entry: no [Desktop Entry] group, or it cannot be read", plain);
        // What the shell provides, only while it does.
        shaodesk::AutostartOptions shell = plain;
        shell.notifications = shell.tray = shell.polkit = true;
        start("[Desktop Entry]\nType=Application\nExec=dunst\n");
        check("[Desktop Entry]\nType=Application\nExec=dunst\n",
              "the shell is the notification daemon", shell);
        check("[Desktop Entry]\nType=Application\nExec=snixembed --fork\n",
              "the shell serves the tray", shell);
        check("[Desktop Entry]\nType=Application\nExec=/usr/libexec/polkit-gnome-authentication-"
              "agent-1\n",
              "the shell is the polkit agent", shell);
        check("[Desktop Entry]\nType=Application\nExec=nm-applet\n", "", shell);

        // The environment's directories and desktops.
        setenv("HOME", (root / "h").c_str(), 1);
        setenv("XDG_CONFIG_HOME", "relative", 1);
        setenv("XDG_CONFIG_DIRS", "/a:relative::/b", 1);
        auto directories = shaodesk::autostart_directories();
        require(directories == std::vector<fs::path>{root / "h/.config/autostart",
                                                     "/a/autostart", "/b/autostart"},
                "directories without XDG_CONFIG_HOME");
        setenv("XDG_CONFIG_HOME", (root / "c").c_str(), 1);
        unsetenv("XDG_CONFIG_DIRS");
        directories = shaodesk::autostart_directories();
        require(directories == std::vector<fs::path>{root / "c/autostart", "/etc/xdg/autostart"},
                "directories by default");
        setenv("XDG_CURRENT_DESKTOP", "shaodesk:wlroots", 1);
        require(shaodesk::current_desktops() == Words{"shaodesk", "wlroots"}, "desktops");

        // The settings.
        auto defaults = shaodesk::parse_config("return {}");
        require(defaults.autostart.xdg && defaults.autostart.exclude.empty(), "defaults");
        auto custom = shaodesk::parse_config(
            "return {autostart={xdg=false,exclude={'a.desktop','org.b.c.desktop'}}}");
        require(!custom.autostart.xdg &&
                    custom.autostart.exclude == Words{"a.desktop", "org.b.c.desktop"},
                "autostart not parsed");
        rejects("return {autostart=true}");
        rejects("return {autostart={xdg='yes'}}");
        rejects("return {autostart={exclude='a.desktop'}}");
        rejects("return {autostart={exclude={'a'}}}");
        rejects("return {autostart={exclude={'.desktop'}}}");
        rejects("return {autostart={exclude={'/etc/xdg/autostart/a.desktop'}}}");
        rejects("return {autostart={exclude={1}}}");
        rejects("return {autostart={xdgs=true}}");
        require(defaults.shell.polkit_agent &&
                    !shaodesk::parse_config("return {shell={polkit_agent=false}}").shell.polkit_agent,
                "shell.polkit_agent not parsed");
        rejects("return {shell={polkit_agent='no'}}");
        require(defaults.settings.session_restore == SH_SESSION_RESTORE_WINDOWS,
                "session.restore's default");
        require(shaodesk::parse_config("return {session={restore='off'}}").settings.session_restore ==
                        SH_SESSION_RESTORE_OFF &&
                    shaodesk::parse_config("return {session={restore='launch'}}")
                            .settings.session_restore == SH_SESSION_RESTORE_LAUNCH,
                "session.restore not parsed");
        rejects("return {session={restore=true}}");
        rejects("return {session={restore='all'}}");
        rejects("return {session={save=true}}");
        std::cout << "autostart passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
    fs::remove_all(root);
    return 0;
}
