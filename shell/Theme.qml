// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

// The shell's design tokens: colours, type, radii, spacing, icon sizes and motion, derived from
// the appearance profile in use (shell.panel_color, text_color, accent, font, font_size and
// windows.urgent_color) and the animation settings, so that every part of the shell draws from
// one set and follows a profile switch at once.
//
// States (hover, pressed, selected) and lines are the text colour laid over what is under them
// at a low opacity. That shows on a light panel as on a dark one, where lightening the panel
// colour does nothing to a near-white one.
QtObject {
    // Surfaces. The bar keeps the configured colour, translucency included. Popups are opaque:
    // nothing is blurred behind them, and a window showing through makes text hard to read.
    readonly property color bar: shell.panelColor
    readonly property color surface: Qt.rgba(bar.r, bar.g, bar.b, 1)
    // A card, field or other block inside a popup.
    readonly property color surfaceRaised: mix(surface, text, 0.04)
    readonly property color surfaceRaisedHover: mix(surface, text, 0.09)
    // Whether the surfaces are light, for the few colours that cannot be derived from them.
    readonly property bool light: luminance(surface) > 0.5

    // States, laid over a surface or the bar.
    readonly property color hover: alpha(text, 0.08)
    readonly property color pressed: alpha(text, 0.18)
    // What is open, current or on: a button whose popup is open, the current workspace.
    readonly property color selected: alpha(text, 0.14)

    // What dims everything behind a dialog.
    readonly property color scrim: Qt.rgba(0, 0, 0, 0.45)

    // Lines: a popup's outline, a separator.
    readonly property color border: alpha(text, 0.14)
    readonly property color divider: alpha(text, 0.12)

    // Text and line icons.
    readonly property color text: shell.textColor
    readonly property color textMuted: alpha(text, 0.62)
    readonly property color textDisabled: alpha(text, 0.4)

    readonly property color accent: shell.accent
    // Text and icons on an accent fill: whichever of the surface and text colours stands out
    // more against it.
    readonly property color textOnAccent: contrast(accent, surface) >= contrast(accent, text)
                                          ? surface : text
    readonly property color accentHover: mix(accent, textOnAccent, 0.15)
    // A selected row or tab that is not filled with the accent.
    readonly property color accentSubtle: alpha(accent, 0.22)

    // Something going wrong: text and outlines on a surface, a fill with textOnDanger on it, and
    // a surface for a message.
    readonly property color danger: light ? "#c42b1c" : "#ff6b6b"
    readonly property color dangerFill: "#c42b1c"
    readonly property color textOnDanger: "#ffffff"
    readonly property color dangerSurface: mix(surface, dangerFill, light ? 0.18 : 0.35)

    // A window asking for attention (windows.urgent_color).
    readonly property color urgent: shell.urgentColor
    readonly property color urgentSubtle: alpha(urgent, 0.24)

    // Type, from shell.font and shell.font_size: body text, then smaller for secondary text and
    // captions, larger for list entries and popup headings, a title for a dialog or an overlay's
    // input, and the launcher's heading.
    readonly property string fontFamily: shell.fontFamily.length > 0 ? shell.fontFamily
                                                                   : Qt.application.font.family
    readonly property int fontSize: shell.fontSize
    readonly property int fontSizeSmall: Math.max(6, fontSize - 1)
    readonly property int fontSizeCaption: Math.max(6, fontSize - 2)
    readonly property int fontSizeLarge: fontSize + 2
    readonly property int fontSizeTitle: fontSize + 4
    readonly property int fontSizeDisplay: Math.round(fontSize * 1.75)

    // Corners: buttons and rows, menus and small popups, large popups.
    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 10
    readonly property int radiusLarge: 14

    readonly property int spacingXS: 2
    readonly property int spacingS: 4
    readonly property int spacingM: 8
    readonly property int spacingL: 12
    readonly property int spacingXL: 16
    readonly property int spacingXXL: 20

    // A row of a menu or a list, and a section's heading over rows.
    readonly property int rowHeight: 36
    readonly property int headingHeight: 30

    // Line icons in menus, line icons on the bar, applications' icons on the bar and in lists,
    // and in the launcher.
    readonly property int iconSizeSmall: 16
    readonly property int iconSize: 18
    readonly property int appIconSize: 22
    readonly property int appIconSizeLarge: 30
    // A line icon saying what an overlay shows, as the on-screen display's.
    readonly property int iconSizeLarge: 22

    // Motion, following animations.enabled and animations.speed: every duration is 0 while
    // animations are off.
    readonly property bool animations: shell.animations
    readonly property real motionScale: animations ? 1 / Math.max(0.1, shell.animationSpeed) : 0
    readonly property int durationFast: duration(120)
    readonly property int durationNormal: duration(180)
    readonly property int durationSlow: duration(300)
    // Something arriving or moving, and something leaving.
    readonly property int easing: Easing.OutCubic
    readonly property int easingExit: Easing.InCubic

    // Whether shader effects (shadows) can be drawn: only through the GPU.
    readonly property bool effects: shell.effects
    // The shadow under a popup (PopupCard), drawn only with effects: how dark, how soft and how
    // far down. A light profile needs less of it to show.
    readonly property color shadow: Qt.rgba(0, 0, 0, light ? 0.3 : 0.6)
    readonly property int shadowBlur: 20
    readonly property int shadowOffset: 6
    // The room a shadow needs around what casts it, which a surface of its own (the on-screen
    // display, the cards, the switcher) leaves so that its edge does not cut the shadow off;
    // none without effects.
    readonly property int shadowMargin: effects ? shadowBlur + shadowOffset : 0

    // `milliseconds` at the animation speed, 0 with animations off.
    function duration(milliseconds) { return Math.round(milliseconds * motionScale) }
    function alpha(color, opacity) { return Qt.rgba(color.r, color.g, color.b, color.a * opacity) }
    // `a` with `amount` (0 to 1) of `b` mixed in.
    function mix(a, b, amount) {
        return Qt.rgba(a.r + (b.r - a.r) * amount, a.g + (b.g - a.g) * amount,
                       a.b + (b.b - a.b) * amount, a.a + (b.a - a.a) * amount)
    }
    // WCAG's relative luminance and contrast ratio.
    function luminance(color) {
        function channel(c) {
            return c <= 0.04045 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4)
        }
        return 0.2126 * channel(color.r) + 0.7152 * channel(color.g) + 0.0722 * channel(color.b)
    }
    function contrast(a, b) {
        var la = luminance(a), lb = luminance(b)
        return (Math.max(la, lb) + 0.05) / (Math.min(la, lb) + 0.05)
    }
}
