// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_match.hpp"
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>

namespace app_match {

QString trimmed(const QString &name) {
    static const QStringList extensions{".desktop", ".exe", ".appimage", ".sh", ".py"};
    static const QStringList suffixes{
        "-wrapped", "-bin", "-desktop",     "-stable", "-beta",   "-nightly", "-git",   "-browser",
        "-wayland", "-x11", "-url-handler", "-x86_64", "_x86_64", ".x86_64",  "-amd64", "-linux"};
    static const QRegularExpression version(R"([-_ ]v?\d+(\.\d+)*$)");
    auto text = name.trimmed().toLower();
    while (text.startsWith('.'))
        text.remove(0, 1);
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto &list : {extensions, suffixes})
            for (const auto &suffix : list)
                if (text.size() > suffix.size() && text.endsWith(suffix)) {
                    text.chop(suffix.size());
                    changed = true;
                }
        if (const auto match = version.match(text); match.hasMatch() && match.capturedStart() > 0) {
            text.truncate(match.capturedStart());
            changed = true;
        }
    }
    return text;
}

QString key(const QString &name) {
    auto text = trimmed(name);
    text.removeIf([](QChar c) { return !c.isLetterOrNumber(); });
    return text;
}

QString program(const QString &exec) {
    static const QStringList wrappers{"env",           "exec",        "nice",     "ionice",
                                      "prime-run",     "gamemoderun", "mangohud", "optirun",
                                      "primusrun",     "pkexec",      "sudo",     "firejail",
                                      "flatpak-spawn", "systemd-run", "uwsm-app"};
    static const QRegularExpression assignment(R"(^[A-Za-z_][A-Za-z0-9_]*=)");
    const auto words = QProcess::splitCommand(exec);
    for (qsizetype i = 0; i < words.size(); ++i) {
        const auto &word = words[i];
        const auto file = QFileInfo(word).fileName();
        // A variable, or a wrapper's option or its value (nice -n 10).
        if (assignment.match(word).hasMatch() || word.startsWith('-') ||
            std::all_of(word.begin(), word.end(), [](QChar c) { return c.isDigit(); }))
            continue;
        if (wrappers.contains(file))
            continue;
        if ((file == "sh" || file == "bash" || file == "zsh" || file == "dash") &&
            i + 2 < words.size() && words[i + 1] == "-c")
            return program(words[i + 2]);
        if (file == "flatpak") {
            QString application;
            for (qsizetype j = i + 1; j < words.size(); ++j) {
                if (words[j].startsWith("--command="))
                    return QFileInfo(words[j].mid(10)).fileName();
                if (!words[j].startsWith('-') && !words[j].startsWith('@') &&
                    !words[j].startsWith('%') && words[j] != "run" && application.isEmpty())
                    application = words[j];
            }
            return application;
        }
        return file;
    }
    return {};
}

} // namespace app_match
