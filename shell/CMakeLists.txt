find_package(Qt6 6.9 REQUIRED COMPONENTS Core Gui Network Qml Quick)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-unix-2.0)
pkg_check_modules(SHELL_WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
# PulseAudio's client library (PipeWire serves it too) drives the panel's volume control.
option(SHAODESK_PULSEAUDIO "Control sound from the panel through libpulse" ON)
if(SHAODESK_PULSEAUDIO)
    pkg_check_modules(PULSE IMPORTED_TARGET libpulse)
    if(NOT PULSE_FOUND)
        message(STATUS "libpulse not found: the panel's volume control is left out")
    endif()
endif()
# The notification daemon and the system tray speak D-Bus; without QtDBus the shell builds
# without them and the panel leaves out the bell and the tray.
option(SHAODESK_NOTIFICATIONS "Serve org.freedesktop.Notifications from the shell (needs Qt6 DBus)" ON)
option(SHAODESK_TRAY "Show applications' status icons (StatusNotifierItem) in the panel (needs Qt6 DBus)" ON)
# Quick Settings' services on D-Bus; without QtDBus they are left out and their widgets never show.
option(SHAODESK_MEDIA "Control media players (MPRIS) from Quick Settings and the media keys (needs Qt6 DBus)" ON)
option(SHAODESK_POWER_PROFILES "Switch power-profiles-daemon's power mode from Quick Settings (needs Qt6 DBus)" ON)
option(SHAODESK_NETWORKMANAGER "Manage Wi-Fi through NetworkManager from Quick Settings and the network widget (needs Qt6 DBus)" ON)
option(SHAODESK_BLUETOOTH "Manage Bluetooth through BlueZ from Quick Settings (needs Qt6 DBus)" ON)
set(SHAODESK_DBUS OFF)
set(SHAODESK_TRAY_DBUS OFF)
set(SHAODESK_MEDIA_DBUS OFF)
set(SHAODESK_POWER_PROFILES_DBUS OFF)
set(SHAODESK_NETWORKMANAGER_DBUS OFF)
set(SHAODESK_BLUETOOTH_DBUS OFF)
if(SHAODESK_NOTIFICATIONS OR SHAODESK_TRAY OR SHAODESK_MEDIA OR SHAODESK_POWER_PROFILES OR SHAODESK_NETWORKMANAGER
   OR SHAODESK_BLUETOOTH)
    find_package(Qt6 6.9 QUIET COMPONENTS DBus)
    if(Qt6DBus_FOUND)
        set(SHAODESK_DBUS ${SHAODESK_NOTIFICATIONS})
        set(SHAODESK_TRAY_DBUS ${SHAODESK_TRAY})
        set(SHAODESK_MEDIA_DBUS ${SHAODESK_MEDIA})
        set(SHAODESK_POWER_PROFILES_DBUS ${SHAODESK_POWER_PROFILES})
        set(SHAODESK_NETWORKMANAGER_DBUS ${SHAODESK_NETWORKMANAGER})
        set(SHAODESK_BLUETOOTH_DBUS ${SHAODESK_BLUETOOTH})
    else()
        message(STATUS "Qt6 DBus not found: the notification daemon, the tray and Quick Settings' D-Bus services are left out")
    endif()
endif()
# The polkit authentication agent asks for the password pkexec, GParted or an updater needs; it
# needs libpolkit-agent-1, and without it the shell builds without the agent.
option(SHAODESK_POLKIT "Be the session's polkit authentication agent (needs polkit-agent-1)" ON)
set(SHAODESK_POLKIT_AGENT OFF)
if(SHAODESK_POLKIT)
    pkg_check_modules(POLKIT_AGENT QUIET IMPORTED_TARGET polkit-agent-1)
    if(POLKIT_AGENT_FOUND)
        set(SHAODESK_POLKIT_AGENT ON)
    else()
        message(STATUS "polkit-agent-1 not found: the polkit authentication agent is left out")
    endif()
endif()
if(NOT SHAODESK_SHELL_PREVIEW_ONLY)
    find_package(LayerShellQt 6.6 QUIET)
    if(NOT LayerShellQt_FOUND)
        message(FATAL_ERROR "The desktop shell requires LayerShellQt 6.6+. Install layer-shell-qt, or use -DSHAODESK_BUILD_SHELL=OFF for the compositor or -DSHAODESK_SHELL_PREVIEW_ONLY=ON for UI development.")
    endif()
endif()

# The start menu's model (its pins, the launch history and its search), in everything that builds
# the controller.
set(SHAODESK_START_SOURCES start_menu.cpp start_menu.hpp launch_history.cpp launch_history.hpp)
# The authentication dialog's model, which the controller holds, in everything that builds it.
set(SHAODESK_AUTH_SOURCES authentication.cpp authentication.hpp)
# The models of the services Quick Settings controls over D-Bus, in everything that builds the
# controller; their D-Bus backends only in the shell.
set(SHAODESK_SERVICE_SOURCES media.cpp media.hpp power_mode.cpp power_mode.hpp wifi.cpp wifi.hpp
    row_model.hpp bluetooth.cpp bluetooth.hpp)
set(SHAODESK_NOTIFY_SOURCES notifications.cpp notifications.hpp notification_images.cpp
    notification_images.hpp osd.cpp osd.hpp backlight.cpp backlight.hpp tray.cpp tray.hpp
    tray_images.cpp tray_images.hpp)
# What the compositor passes on from the volume, microphone and brightness keys, which feeds the
# on-screen display too.
list(APPEND SHAODESK_NOTIFY_SOURCES volume_keys.cpp volume_keys.hpp)
# The display mode popup's model, which the controller holds too, and the display settings
# window's.
list(APPEND SHAODESK_NOTIFY_SOURCES display_modes.cpp display_modes.hpp display_settings.cpp
    display_settings.hpp display_layout.cpp display_layout.hpp)
# The QML is compiled ahead of time into the binary (qmlcachegen), so starting the shell parses
# and compiles none of it. It lives at qrc:/shell/ShaodeskShell/*.qml.
qt_add_library(shaodesk_shell_qml STATIC)
# Theme is the design tokens every QML file reads, one instance per engine.
set_source_files_properties(Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
# The menus that use WindowMenu.js import it by its path, so the module leaves it out of its qmldir,
# which would evaluate it for every file.
set_source_files_properties(WindowMenu.js PROPERTIES QT_QML_SKIP_QMLDIR_ENTRY TRUE)
qt_add_qml_module(shaodesk_shell_qml URI ShaodeskShell VERSION 1.0 RESOURCE_PREFIX /shell
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ShaodeskShell
    NO_PLUGIN NO_GENERATE_QMLTYPES
    QML_FILES Theme.qml EmptyState.qml FlatButton.qml ButtonFill.qml PushButton.qml TextButton.qml CloseButton.qml SearchInput.qml PopupCard.qml PopupMenu.qml MenuRow.qml Panel.qml Taskbar.qml Icon.qml FadingIcon.qml Desktop.qml Switcher.qml SwitcherCards.qml Palette.qml Overview.qml NotificationCards.qml Osd.qml ConfigError.qml NotificationBell.qml NotificationHistory.qml
    KeyboardLayout.qml BarTip.qml BarAppIcon.qml Badge.qml TaskButton.qml TaskList.qml PinnedSlots.qml TrayButton.qml
    WorkspaceIndicator.qml VolumeButton.qml ClockButton.qml BatteryWidget.qml NetworkWidget.qml SpeakerIcon.qml AudioSlider.qml MuteButton.qml StreamRow.qml
    AudioMixer.qml
    CalendarPopup.qml
    AudioOutputs.qml
    ProfileList.qml
    WallpaperPicker.qml
    Launcher.qml
    Launchpad.qml
    DrawnWallpaper.qml
    StartHome.qml
    StartAllApps.qml
    StartSearch.qml
    StartBestMatch.qml
    StartTile.qml
    StartRow.qml
    UserAvatar.qml
    PowerMenu.qml
    TaskbarMenu.qml
    WindowMenu.js
    TrayMenu.qml
    GroupList.qml
    WindowThumbnails.qml
    BarKeyboard.qml
    FocusRing.qml

    PowerDialog.qml
    AuthDialog.qml
    ClockFlyout.qml
    BatteryIcon.qml
    QuickSettingsButton.qml
    WallpapersButton.qml
    ProfilesButton.qml
    TilingButton.qml
    Dock.qml
    DockIcon.qml
    TopMenuBar.qml
    MenuBarMenu.qml
    QuickSettings.qml
    QuickTile.qml
    BindingMode.qml
    MediaCard.qml
    WifiList.qml
    WifiPopup.qml
    BluetoothList.qml
    DisplayMode.qml)
# The protocols task_model.cpp and window_pictures.cpp speak, generated in the top-level build
# directory.
set(SHAODESK_TASK_PROTOCOL_SOURCES
    ${PROJECT_BINARY_DIR}/shaodesk-window-control-v1-protocol.c ${PROJECT_BINARY_DIR}/shaodesk-window-control-v1-client-protocol.h
    ${PROJECT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-protocol.c
    ${PROJECT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h
    ${PROJECT_BINARY_DIR}/ext-image-copy-capture-v1-protocol.c
    ${PROJECT_BINARY_DIR}/ext-image-copy-capture-v1-client-protocol.h
    ${CAPTURE_SOURCE_PROTOCOL_SOURCES})
add_executable(shaodesk-shell main.cpp ${SHAODESK_NOTIFY_SOURCES} controller.cpp controller.hpp palette.cpp palette.hpp fuzzy.cpp fuzzy.hpp task_model.cpp task_model.hpp window_pictures.cpp window_pictures.hpp
    task_filter.cpp task_filter.hpp icons.cpp icons.hpp app_match.cpp app_match.hpp wallpapers.cpp wallpapers.hpp window_images.cpp window_images.hpp view.cpp view.hpp audio.cpp audio.hpp system_status.cpp system_status.hpp power.cpp power.hpp
    preview.cpp preview.hpp
    ${SHAODESK_TASK_PROTOCOL_SOURCES})
add_dependencies(shaodesk-shell shaodesk_wlr_client_protocols shaodesk_version)
set_target_properties(shaodesk-shell PROPERTIES AUTOMOC ON RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
target_sources(shaodesk-shell PRIVATE ${SHAODESK_START_SOURCES})
target_sources(shaodesk-shell PRIVATE ${SHAODESK_AUTH_SOURCES})
target_sources(shaodesk-shell PRIVATE ${SHAODESK_SERVICE_SOURCES})
target_include_directories(shaodesk-shell PRIVATE ${PROJECT_BINARY_DIR})
target_link_libraries(shaodesk-shell PRIVATE shaodesk_config Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick
    PkgConfig::GIO PkgConfig::SHELL_WAYLAND)
target_compile_definitions(shaodesk-shell PRIVATE QT_NO_KEYWORDS)
if(SHAODESK_DBUS)
    target_sources(shaodesk-shell PRIVATE notification_service.cpp notification_service.hpp)
    target_link_libraries(shaodesk-shell PRIVATE Qt6::DBus)
    target_compile_definitions(shaodesk-shell PRIVATE SHAODESK_DBUS=1)
    # XDG autostart then leaves another notification daemon out.
    if(TARGET shaodesk)
        target_compile_definitions(shaodesk PRIVATE SHAODESK_SHELL_NOTIFICATIONS=1)
    endif()
endif()
if(SHAODESK_TRAY_DBUS)
    target_sources(shaodesk-shell PRIVATE tray_host.cpp tray_host.hpp tray_watcher.cpp tray_watcher.hpp)
    target_link_libraries(shaodesk-shell PRIVATE Qt6::DBus)
    target_compile_definitions(shaodesk-shell PRIVATE SHAODESK_TRAY=1)
    # XDG autostart then leaves another StatusNotifierWatcher out.
    if(TARGET shaodesk)
        target_compile_definitions(shaodesk PRIVATE SHAODESK_SHELL_TRAY=1)
    endif()
endif()
if(SHAODESK_POLKIT_AGENT)
    target_sources(shaodesk-shell PRIVATE polkit_agent.cpp polkit_agent.hpp)
    target_link_libraries(shaodesk-shell PRIVATE PkgConfig::POLKIT_AGENT)
    target_compile_definitions(shaodesk-shell PRIVATE SHAODESK_POLKIT=1)
    # XDG autostart then leaves another polkit agent out.
    if(TARGET shaodesk)
        target_compile_definitions(shaodesk PRIVATE SHAODESK_SHELL_POLKIT=1)
    endif()
endif()
if(SHAODESK_MEDIA_DBUS)
    target_sources(shaodesk-shell PRIVATE mpris.cpp mpris.hpp)
    target_link_libraries(shaodesk-shell PRIVATE Qt6::DBus)
    target_compile_definitions(shaodesk-shell PRIVATE SHAODESK_MEDIA=1)
endif()
if(SHAODESK_POWER_PROFILES_DBUS)
    target_sources(shaodesk-shell PRIVATE power_profiles_daemon.cpp power_profiles_daemon.hpp)
    target_link_libraries(shaodesk-shell PRIVATE Qt6::DBus)
    target_compile_definitions(shaodesk-shell PRIVATE SHAODESK_POWER_PROFILES=1)
endif()
if(SHAODESK_NETWORKMANAGER_DBUS)
    target_sources(shaodesk-shell PRIVATE network_manager.cpp network_manager.hpp)
    target_link_libraries(shaodesk-shell PRIVATE Qt6::DBus)
    target_compile_definitions(shaodesk-shell PRIVATE SHAODESK_NETWORKMANAGER=1)
endif()
if(SHAODESK_BLUETOOTH_DBUS)
    target_sources(shaodesk-shell PRIVATE bluez.cpp bluez.hpp)
    target_link_libraries(shaodesk-shell PRIVATE Qt6::DBus)
    target_compile_definitions(shaodesk-shell PRIVATE SHAODESK_BLUETOOTH=1)
endif()
if(SHAODESK_PULSEAUDIO AND PULSE_FOUND)
    target_sources(shaodesk-shell PRIVATE pulse_audio.cpp)
    target_link_libraries(shaodesk-shell PRIVATE PkgConfig::PULSE)
    target_compile_definitions(shaodesk-shell PRIVATE SHAODESK_PULSE=1)
endif()
target_compile_options(shaodesk-shell PRIVATE -Wall -Wextra -Wpedantic)
if(SHAODESK_SHELL_PREVIEW_ONLY)
    target_compile_definitions(shaodesk-shell PRIVATE SHAODESK_LAYER_SHELL=0)
else()
    target_link_libraries(shaodesk-shell PRIVATE LayerShellQt::Interface)
    target_compile_definitions(shaodesk-shell PRIVATE SHAODESK_LAYER_SHELL=1)
    install(TARGETS shaodesk-shell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
target_link_libraries(shaodesk-shell PRIVATE shaodesk_shell_qml)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_package(Qt6 6.9 REQUIRED COMPONENTS Test)
    add_executable(shell_ui_test ${PROJECT_SOURCE_DIR}/tests/shell_ui_test.cpp
        ${SHAODESK_NOTIFY_SOURCES} controller.cpp controller.hpp palette.cpp palette.hpp fuzzy.cpp fuzzy.hpp view.cpp view.hpp task_model.cpp task_model.hpp window_pictures.cpp window_pictures.hpp
        task_filter.cpp task_filter.hpp icons.cpp icons.hpp app_match.cpp app_match.hpp wallpapers.cpp wallpapers.hpp window_images.cpp window_images.hpp audio.cpp audio.hpp system_status.cpp system_status.hpp power.cpp power.hpp
        ${SHAODESK_TASK_PROTOCOL_SOURCES})
    set_target_properties(shell_ui_test PROPERTIES AUTOMOC ON)
    target_sources(shell_ui_test PRIVATE ${SHAODESK_START_SOURCES})
    target_sources(shell_ui_test PRIVATE ${SHAODESK_AUTH_SOURCES})
    target_sources(shell_ui_test PRIVATE ${SHAODESK_SERVICE_SOURCES})
    target_include_directories(shell_ui_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
    target_compile_definitions(shell_ui_test PRIVATE QT_NO_KEYWORDS SHAODESK_LAYER_SHELL=0)
    target_compile_options(shell_ui_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(shell_ui_test PRIVATE shaodesk_config Qt6::Test Qt6::Network Qt6::Quick Qt6::Qml
        PkgConfig::GIO PkgConfig::SHELL_WAYLAND)
    target_link_libraries(shell_ui_test PRIVATE shaodesk_shell_qml)
    add_test(NAME shell_ui COMMAND shell_ui_test ${CMAKE_COMMAND})
    set_tests_properties(shell_ui PROPERTIES TIMEOUT 240
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(task_filter_test ${PROJECT_SOURCE_DIR}/tests/task_filter_test.cpp
        ${SHAODESK_NOTIFY_SOURCES} controller.cpp controller.hpp palette.cpp palette.hpp fuzzy.cpp fuzzy.hpp view.cpp view.hpp task_model.cpp task_model.hpp window_pictures.cpp window_pictures.hpp
        task_filter.cpp task_filter.hpp icons.cpp icons.hpp app_match.cpp app_match.hpp wallpapers.cpp wallpapers.hpp window_images.cpp window_images.hpp audio.cpp audio.hpp system_status.cpp system_status.hpp power.cpp power.hpp
        ${SHAODESK_TASK_PROTOCOL_SOURCES})
    set_target_properties(task_filter_test PROPERTIES AUTOMOC ON)
    target_sources(task_filter_test PRIVATE ${SHAODESK_START_SOURCES})
    target_sources(task_filter_test PRIVATE ${SHAODESK_AUTH_SOURCES})
    target_sources(task_filter_test PRIVATE ${SHAODESK_SERVICE_SOURCES})
    target_include_directories(task_filter_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
    target_compile_definitions(task_filter_test PRIVATE QT_NO_KEYWORDS SHAODESK_LAYER_SHELL=0)
    target_compile_options(task_filter_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(task_filter_test PRIVATE shaodesk_config Qt6::Test Qt6::Network Qt6::Quick Qt6::Qml
        PkgConfig::GIO PkgConfig::SHELL_WAYLAND)
    target_link_libraries(task_filter_test PRIVATE shaodesk_shell_qml)
    add_test(NAME shell_task_filter COMMAND task_filter_test)
    set_tests_properties(shell_task_filter PROPERTIES TIMEOUT 90
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(palette_test ${PROJECT_SOURCE_DIR}/tests/palette_test.cpp fuzzy.cpp fuzzy.hpp)
    set_target_properties(palette_test PROPERTIES AUTOMOC ON)
    target_include_directories(palette_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(palette_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(palette_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(palette_test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME palette COMMAND palette_test)
    # The display settings window's arrangement: snapping, settling, starting at 0, 0.
    add_executable(display_layout_test ${PROJECT_SOURCE_DIR}/tests/display_layout_test.cpp
        display_layout.cpp display_layout.hpp)
    set_target_properties(display_layout_test PROPERTIES AUTOMOC ON)
    target_include_directories(display_layout_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(display_layout_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(display_layout_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(display_layout_test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME shell_display_layout COMMAND display_layout_test)
    # The display settings window's model, against a stand-in for the compositor.
    add_executable(display_settings_test ${PROJECT_SOURCE_DIR}/tests/display_settings_test.cpp
        display_settings.cpp display_settings.hpp display_layout.cpp display_layout.hpp)
    set_target_properties(display_settings_test PROPERTIES AUTOMOC ON)
    target_include_directories(display_settings_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(display_settings_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(display_settings_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(display_settings_test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME shell_display_settings COMMAND display_settings_test)
    # The start menu's model: its pins, the launch history and its search.
    add_executable(start_menu_test ${PROJECT_SOURCE_DIR}/tests/start_menu_test.cpp
        ${SHAODESK_START_SOURCES} fuzzy.cpp fuzzy.hpp)
    set_target_properties(start_menu_test PROPERTIES AUTOMOC ON)
    target_include_directories(start_menu_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(start_menu_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(start_menu_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(start_menu_test PRIVATE Qt6::Core Qt6::Test PkgConfig::GIO)
    add_test(NAME start_menu COMMAND start_menu_test)
    # The authentication dialog's model, with stand-ins for polkit's helper.
    add_executable(authentication_test ${PROJECT_SOURCE_DIR}/tests/authentication_test.cpp
        authentication.cpp authentication.hpp)
    set_target_properties(authentication_test PROPERTIES AUTOMOC ON)
    target_include_directories(authentication_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(authentication_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(authentication_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(authentication_test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME shell_authentication COMMAND authentication_test)
    if(SHAODESK_POLKIT_AGENT)
        # The agent against a stand-in for polkitd on a private bus, the system bus the
        # environment names made unusable.
        add_executable(fake_polkitd ${PROJECT_SOURCE_DIR}/tests/fake_polkitd.c)
        target_compile_options(fake_polkitd PRIVATE -Wall -Wextra -Wno-unused-parameter)
        target_link_libraries(fake_polkitd PRIVATE PkgConfig::GIO)
        add_executable(polkit_agent_test ${PROJECT_SOURCE_DIR}/tests/polkit_agent_test.cpp
            ${SHAODESK_AUTH_SOURCES} polkit_agent.cpp polkit_agent.hpp)
        set_target_properties(polkit_agent_test PROPERTIES AUTOMOC ON)
        target_include_directories(polkit_agent_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(polkit_agent_test PRIVATE QT_NO_KEYWORDS
            FAKE_POLKITD="$<TARGET_FILE:fake_polkitd>")
        target_compile_options(polkit_agent_test PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(polkit_agent_test PRIVATE Qt6::Core Qt6::Test PkgConfig::POLKIT_AGENT)
        add_test(NAME shell_polkit_agent COMMAND polkit_agent_test)
        set_tests_properties(shell_polkit_agent PROPERTIES TIMEOUT 60
            ENVIRONMENT "DBUS_SESSION_BUS_ADDRESS=disabled:;DBUS_SYSTEM_BUS_ADDRESS=disabled:")
    endif()
    # Applications' icons: the desktop's icon theme, and which entry a window belongs to.
    add_executable(app_icons_test ${PROJECT_SOURCE_DIR}/tests/app_icons_test.cpp icons.cpp icons.hpp
        app_match.cpp app_match.hpp)
    set_target_properties(app_icons_test PROPERTIES AUTOMOC ON)
    target_include_directories(app_icons_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(app_icons_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(app_icons_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(app_icons_test PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Test PkgConfig::GIO)
    add_test(NAME app_icons COMMAND app_icons_test)
    add_executable(task_model_test ${PROJECT_SOURCE_DIR}/tests/task_model_test.cpp
        task_model.cpp task_model.hpp window_pictures.cpp window_pictures.hpp window_images.cpp
        window_images.hpp ${SHAODESK_TASK_PROTOCOL_SOURCES})
    set_target_properties(task_model_test PROPERTIES AUTOMOC ON)
    target_include_directories(task_model_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
    target_compile_definitions(task_model_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(task_model_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(task_model_test PRIVATE Qt6::Core Qt6::Gui Qt6::Quick PkgConfig::SHELL_WAYLAND)
    add_test(NAME shell_tasks COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/compositor_smoke.py $<TARGET_FILE:shaodesk>
        $<TARGET_FILE:wayland_probe> ${EXAMPLE_CONFIG}
        $<TARGET_FILE:task_model_test>)
    set_tests_properties(shell_tasks PROPERTIES TIMEOUT 60)
    # The window pictures' formats and sizes, without a compositor.
    add_executable(window_pictures_test ${PROJECT_SOURCE_DIR}/tests/window_pictures_test.cpp
        window_pictures.cpp window_pictures.hpp ${SHAODESK_TASK_PROTOCOL_SOURCES})
    set_target_properties(window_pictures_test PROPERTIES AUTOMOC ON)
    target_include_directories(window_pictures_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
    target_compile_definitions(window_pictures_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(window_pictures_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(window_pictures_test PRIVATE Qt6::Gui Qt6::Test PkgConfig::SHELL_WAYLAND)
    add_test(NAME shell_window_pictures COMMAND window_pictures_test)
    add_executable(notifications_test ${PROJECT_SOURCE_DIR}/tests/notifications_test.cpp
        notifications.cpp notifications.hpp)
    set_target_properties(notifications_test PROPERTIES AUTOMOC ON)
    target_include_directories(notifications_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(notifications_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(notifications_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(notifications_test PRIVATE shaodesk_config Qt6::Gui Qt6::Test)
    add_test(NAME shell_notifications COMMAND notifications_test)
    set_tests_properties(shell_notifications PROPERTIES TIMEOUT 90
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(SHAODESK_DBUS)
        add_executable(notifications_dbus_test ${PROJECT_SOURCE_DIR}/tests/notifications_dbus_test.cpp
            notifications.cpp notifications.hpp notification_service.cpp notification_service.hpp)
        set_target_properties(notifications_dbus_test PROPERTIES AUTOMOC ON)
        target_include_directories(notifications_dbus_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(notifications_dbus_test PRIVATE QT_NO_KEYWORDS)
        target_compile_options(notifications_dbus_test PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(notifications_dbus_test PRIVATE shaodesk_config Qt6::Gui Qt6::DBus Qt6::Test)
        # The test talks to a bus of its own; the environment's is made unusable so nothing can
        # reach the real one.
        add_executable(notify_probe ${PROJECT_SOURCE_DIR}/tests/notify_probe.cpp)
        set_target_properties(notify_probe PROPERTIES AUTOMOC ON RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
        target_link_libraries(notify_probe PRIVATE Qt6::Core Qt6::DBus)
        add_test(NAME shell_notifications_dbus COMMAND notifications_dbus_test)
        set_tests_properties(shell_notifications_dbus PROPERTIES TIMEOUT 60
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DBUS_SESSION_BUS_ADDRESS=disabled:")
        # Setting the backlight through a stand-in for logind on a private bus too, with the
        # system bus made unusable as well.
        add_executable(backlight_dbus_test ${PROJECT_SOURCE_DIR}/tests/backlight_dbus_test.cpp
            backlight.cpp backlight.hpp)
        set_target_properties(backlight_dbus_test PROPERTIES AUTOMOC ON)
        target_include_directories(backlight_dbus_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(backlight_dbus_test PRIVATE QT_NO_KEYWORDS SHAODESK_DBUS=1)
        target_compile_options(backlight_dbus_test PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(backlight_dbus_test PRIVATE Qt6::Core Qt6::DBus Qt6::Test)
        add_test(NAME shell_backlight_dbus COMMAND backlight_dbus_test)
        set_tests_properties(shell_backlight_dbus PROPERTIES TIMEOUT 60
            ENVIRONMENT "DBUS_SESSION_BUS_ADDRESS=disabled:;DBUS_SYSTEM_BUS_ADDRESS=disabled:")
    endif()
    if(SHAODESK_TRAY_DBUS)
        # A tray item for the end-to-end tests; tests/tray_fake_item.hpp is its item and menu.
        add_executable(tray_probe ${PROJECT_SOURCE_DIR}/tests/tray_probe.cpp
            ${PROJECT_SOURCE_DIR}/tests/tray_fake_item.hpp)
        set_target_properties(tray_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
        target_compile_definitions(tray_probe PRIVATE QT_NO_KEYWORDS)
        target_compile_options(tray_probe PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(tray_probe PRIVATE Qt6::Gui Qt6::DBus)
        # Like the notification test, on a bus of its own with the environment's made unusable.
        add_executable(tray_dbus_test ${PROJECT_SOURCE_DIR}/tests/tray_dbus_test.cpp
            ${PROJECT_SOURCE_DIR}/tests/tray_fake_item.hpp tray.cpp tray.hpp tray_host.cpp tray_host.hpp
            tray_watcher.cpp tray_watcher.hpp)
        set_target_properties(tray_dbus_test PROPERTIES AUTOMOC ON)
        target_include_directories(tray_dbus_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(tray_dbus_test PRIVATE QT_NO_KEYWORDS)
        target_compile_options(tray_dbus_test PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(tray_dbus_test PRIVATE Qt6::Gui Qt6::DBus Qt6::Test)
        add_test(NAME shell_tray_dbus COMMAND tray_dbus_test)
        set_tests_properties(shell_tray_dbus PROPERTIES TIMEOUT 90
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DBUS_SESSION_BUS_ADDRESS=disabled:")
    endif()
    add_executable(osd_test ${PROJECT_SOURCE_DIR}/tests/osd_test.cpp
        ${SHAODESK_NOTIFY_SOURCES} controller.cpp controller.hpp palette.cpp palette.hpp fuzzy.cpp fuzzy.hpp view.cpp view.hpp task_model.cpp task_model.hpp window_pictures.cpp window_pictures.hpp
        task_filter.cpp task_filter.hpp icons.cpp icons.hpp app_match.cpp app_match.hpp wallpapers.cpp wallpapers.hpp window_images.cpp window_images.hpp audio.cpp audio.hpp system_status.cpp system_status.hpp power.cpp power.hpp
        ${SHAODESK_TASK_PROTOCOL_SOURCES})
    set_target_properties(osd_test PROPERTIES AUTOMOC ON)
    target_sources(osd_test PRIVATE ${SHAODESK_START_SOURCES})
    target_sources(osd_test PRIVATE ${SHAODESK_AUTH_SOURCES})
    target_sources(osd_test PRIVATE ${SHAODESK_SERVICE_SOURCES})
    target_include_directories(osd_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
    target_compile_definitions(osd_test PRIVATE QT_NO_KEYWORDS SHAODESK_LAYER_SHELL=0)
    target_compile_options(osd_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(osd_test PRIVATE shaodesk_config Qt6::Test Qt6::Network Qt6::Quick Qt6::Qml
        PkgConfig::GIO PkgConfig::SHELL_WAYLAND)
    target_link_libraries(osd_test PRIVATE shaodesk_shell_qml)
    add_test(NAME shell_osd COMMAND osd_test)
    set_tests_properties(shell_osd PROPERTIES TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;DBUS_SESSION_BUS_ADDRESS=disabled:")
    # The authentication dialog in its view, with stand-ins for polkit's helper.
    add_executable(auth_dialog_test ${PROJECT_SOURCE_DIR}/tests/auth_dialog_test.cpp
        ${SHAODESK_NOTIFY_SOURCES} controller.cpp controller.hpp palette.cpp palette.hpp fuzzy.cpp fuzzy.hpp view.cpp view.hpp task_model.cpp task_model.hpp window_pictures.cpp window_pictures.hpp
        task_filter.cpp task_filter.hpp icons.cpp icons.hpp app_match.cpp app_match.hpp wallpapers.cpp wallpapers.hpp window_images.cpp window_images.hpp audio.cpp audio.hpp system_status.cpp system_status.hpp power.cpp power.hpp
        ${SHAODESK_TASK_PROTOCOL_SOURCES})
    set_target_properties(auth_dialog_test PROPERTIES AUTOMOC ON)
    target_sources(auth_dialog_test PRIVATE ${SHAODESK_START_SOURCES} ${SHAODESK_AUTH_SOURCES}
                   ${SHAODESK_SERVICE_SOURCES})
    target_include_directories(auth_dialog_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
    target_compile_definitions(auth_dialog_test PRIVATE QT_NO_KEYWORDS SHAODESK_LAYER_SHELL=0)
    target_compile_options(auth_dialog_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(auth_dialog_test PRIVATE shaodesk_config Qt6::Test Qt6::Network Qt6::Quick Qt6::Qml
        PkgConfig::GIO PkgConfig::SHELL_WAYLAND shaodesk_shell_qml)
    add_test(NAME shell_auth_dialog COMMAND auth_dialog_test)
    set_tests_properties(shell_auth_dialog PROPERTIES TIMEOUT 60
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;DBUS_SESSION_BUS_ADDRESS=disabled:")
    add_executable(system_status_test ${PROJECT_SOURCE_DIR}/tests/system_status_test.cpp
        system_status.cpp system_status.hpp)
    set_target_properties(system_status_test PROPERTIES AUTOMOC ON)
    target_include_directories(system_status_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(system_status_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(system_status_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(system_status_test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME shell_system_status COMMAND system_status_test)
    set_tests_properties(shell_system_status PROPERTIES TIMEOUT 90)
    # The volume, microphone and brightness keys, on stand-ins for the sound server, a backlight
    # and wpctl.
    add_executable(volume_keys_test ${PROJECT_SOURCE_DIR}/tests/volume_keys_test.cpp
        volume_keys.cpp volume_keys.hpp audio.cpp audio.hpp backlight.cpp backlight.hpp osd.cpp
        osd.hpp)
    set_target_properties(volume_keys_test PROPERTIES AUTOMOC ON)
    target_include_directories(volume_keys_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(volume_keys_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(volume_keys_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(volume_keys_test PRIVATE shaodesk_config Qt6::Core Qt6::Test)
    add_test(NAME shell_volume_keys COMMAND volume_keys_test)
    set_tests_properties(shell_volume_keys PROPERTIES TIMEOUT 30)
    # The display mode popup's model.
    add_executable(display_modes_test ${PROJECT_SOURCE_DIR}/tests/display_modes_test.cpp
        display_modes.cpp display_modes.hpp)
    set_target_properties(display_modes_test PROPERTIES AUTOMOC ON)
    target_include_directories(display_modes_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(display_modes_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(display_modes_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(display_modes_test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME shell_display_modes COMMAND display_modes_test)
    set_tests_properties(shell_display_modes PROPERTIES TIMEOUT 30)
    # Which window's process plays a stream of the sound server, on a made-up procfs and the real
    # one.
    add_executable(window_sound_test ${PROJECT_SOURCE_DIR}/tests/window_sound_test.cpp
        audio.cpp audio.hpp)
    set_target_properties(window_sound_test PROPERTIES AUTOMOC ON)
    target_include_directories(window_sound_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(window_sound_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(window_sound_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(window_sound_test PRIVATE Qt6::Gui Qt6::Test)
    add_test(NAME shell_window_sound COMMAND window_sound_test)
    # The media players' model: their order, the current one, and what the controls ask of it.
    add_executable(media_test ${PROJECT_SOURCE_DIR}/tests/media_test.cpp media.cpp media.hpp)
    set_target_properties(media_test PROPERTIES AUTOMOC ON)
    target_include_directories(media_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(media_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(media_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(media_test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME shell_media COMMAND media_test)
    # Wi-Fi's model: how networks are secured and listed, and what the controls ask for.
    add_executable(wifi_test ${PROJECT_SOURCE_DIR}/tests/wifi_test.cpp wifi.cpp wifi.hpp)
    set_target_properties(wifi_test PROPERTIES AUTOMOC ON)
    target_include_directories(wifi_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(wifi_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(wifi_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(wifi_test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME shell_wifi COMMAND wifi_test)
    # Bluetooth's model: its lists of devices, what the controls ask for, and the answers to BlueZ.
    add_executable(bluetooth_test ${PROJECT_SOURCE_DIR}/tests/bluetooth_test.cpp bluetooth.cpp bluetooth.hpp)
    set_target_properties(bluetooth_test PROPERTIES AUTOMOC ON)
    target_include_directories(bluetooth_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(bluetooth_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(bluetooth_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(bluetooth_test PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME shell_bluetooth COMMAND bluetooth_test)
    if(SHAODESK_MEDIA_DBUS)
        # The MPRIS backend against stand-in players on a bus of its own, the environment's made
        # unusable.
        add_executable(media_dbus_test ${PROJECT_SOURCE_DIR}/tests/media_dbus_test.cpp
            ${PROJECT_SOURCE_DIR}/tests/fake_dbus.hpp media.cpp media.hpp mpris.cpp mpris.hpp)
        set_target_properties(media_dbus_test PROPERTIES AUTOMOC ON)
        target_include_directories(media_dbus_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(media_dbus_test PRIVATE QT_NO_KEYWORDS SHAODESK_MEDIA=1)
        target_compile_options(media_dbus_test PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(media_dbus_test PRIVATE Qt6::Core Qt6::DBus Qt6::Test)
        add_test(NAME shell_media_dbus COMMAND media_dbus_test)
        set_tests_properties(shell_media_dbus PROPERTIES TIMEOUT 60
            ENVIRONMENT "DBUS_SESSION_BUS_ADDRESS=disabled:;DBUS_SYSTEM_BUS_ADDRESS=disabled:")
        # A media player for the end-to-end test (media_smoke), as a program of its own.
        add_executable(mpris_probe ${PROJECT_SOURCE_DIR}/tests/mpris_probe.cpp
            ${PROJECT_SOURCE_DIR}/tests/fake_dbus.hpp)
        set_target_properties(mpris_probe PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
        target_compile_definitions(mpris_probe PRIVATE QT_NO_KEYWORDS)
        target_compile_options(mpris_probe PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(mpris_probe PRIVATE Qt6::Core Qt6::DBus)
    endif()
    if(SHAODESK_POWER_PROFILES_DBUS)
        # The power mode against a stand-in power-profiles-daemon on a bus of its own.
        add_executable(power_mode_dbus_test ${PROJECT_SOURCE_DIR}/tests/power_mode_dbus_test.cpp
            ${PROJECT_SOURCE_DIR}/tests/fake_dbus.hpp power_mode.cpp power_mode.hpp
            power_profiles_daemon.cpp power_profiles_daemon.hpp)
        set_target_properties(power_mode_dbus_test PROPERTIES AUTOMOC ON)
        target_include_directories(power_mode_dbus_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(power_mode_dbus_test PRIVATE QT_NO_KEYWORDS SHAODESK_POWER_PROFILES=1)
        target_compile_options(power_mode_dbus_test PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(power_mode_dbus_test PRIVATE Qt6::Core Qt6::DBus Qt6::Test)
        add_test(NAME shell_power_mode_dbus COMMAND power_mode_dbus_test)
        set_tests_properties(shell_power_mode_dbus PROPERTIES TIMEOUT 60
            ENVIRONMENT "DBUS_SESSION_BUS_ADDRESS=disabled:;DBUS_SYSTEM_BUS_ADDRESS=disabled:")
    endif()
    if(SHAODESK_NETWORKMANAGER_DBUS)
        # Wi-Fi against a stand-in NetworkManager on a bus of its own.
        add_executable(network_manager_dbus_test ${PROJECT_SOURCE_DIR}/tests/network_manager_dbus_test.cpp
            ${PROJECT_SOURCE_DIR}/tests/fake_dbus.hpp wifi.cpp wifi.hpp network_manager.cpp network_manager.hpp)
        set_target_properties(network_manager_dbus_test PROPERTIES AUTOMOC ON)
        target_include_directories(network_manager_dbus_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(network_manager_dbus_test PRIVATE QT_NO_KEYWORDS SHAODESK_NETWORKMANAGER=1)
        target_compile_options(network_manager_dbus_test PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(network_manager_dbus_test PRIVATE Qt6::Core Qt6::DBus Qt6::Test)
        add_test(NAME shell_network_manager_dbus COMMAND network_manager_dbus_test)
        set_tests_properties(shell_network_manager_dbus PROPERTIES TIMEOUT 60
            ENVIRONMENT "DBUS_SESSION_BUS_ADDRESS=disabled:;DBUS_SYSTEM_BUS_ADDRESS=disabled:")
    endif()
    if(SHAODESK_BLUETOOTH_DBUS)
        # Bluetooth against a stand-in BlueZ on a bus of its own, which calls the shell's agent.
        add_executable(bluez_dbus_test ${PROJECT_SOURCE_DIR}/tests/bluez_dbus_test.cpp
            ${PROJECT_SOURCE_DIR}/tests/fake_dbus.hpp bluetooth.cpp bluetooth.hpp bluez.cpp bluez.hpp)
        set_target_properties(bluez_dbus_test PROPERTIES AUTOMOC ON)
        target_include_directories(bluez_dbus_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(bluez_dbus_test PRIVATE QT_NO_KEYWORDS SHAODESK_BLUETOOTH=1)
        target_compile_options(bluez_dbus_test PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(bluez_dbus_test PRIVATE Qt6::Core Qt6::DBus Qt6::Test)
        add_test(NAME shell_bluez_dbus COMMAND bluez_dbus_test)
        set_tests_properties(shell_bluez_dbus PROPERTIES TIMEOUT 60
            ENVIRONMENT "DBUS_SESSION_BUS_ADDRESS=disabled:;DBUS_SYSTEM_BUS_ADDRESS=disabled:")
    endif()
    add_test(NAME shell_preview COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/shell_preview.py $<TARGET_FILE:shaodesk-shell>
        ${EXAMPLE_CONFIG})
    set_tests_properties(shell_preview PROPERTIES TIMEOUT 90)
    # Every popup of tools/shell_gallery.py renders without a QML warning.
    add_test(NAME shell_gallery COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/shell_gallery.py ${PROJECT_SOURCE_DIR}/tools/shell_gallery.py
        ${PROJECT_BINARY_DIR} software)
    set_tests_properties(shell_gallery PROPERTIES TIMEOUT 120)
    add_test(NAME shell_version COMMAND shaodesk-shell --version)
    set_tests_properties(shell_version PROPERTIES PASS_REGULAR_EXPRESSION "^shaodesk-shell ${PROJECT_VERSION}( \\(git [^)]+\\))?\n$")
    if(NOT SHAODESK_SHELL_PREVIEW_ONLY)
        add_test(NAME shell_integration COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/shell_integration.py $<TARGET_FILE:shaodesk>
            $<TARGET_FILE:shaodesk-shell> $<TARGET_FILE:wayland_probe>
            ${EXAMPLE_CONFIG})
        set_tests_properties(shell_integration PROPERTIES TIMEOUT 90)
        add_test(NAME overview_shell_smoke COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/overview_shell_smoke.py $<TARGET_FILE:shaodesk>
            $<TARGET_FILE:shaodesk-shell> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(overview_shell_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME snap_assist_shell_smoke COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/snap_assist_shell_smoke.py $<TARGET_FILE:shaodesk>
            $<TARGET_FILE:shaodesk-shell> $<TARGET_FILE:wayland_probe>
            $<TARGET_FILE:pointer_probe>)
        set_tests_properties(snap_assist_shell_smoke PROPERTIES TIMEOUT 60)
        add_test(NAME overlay_exit_smoke COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/overlay_exit_smoke.py $<TARGET_FILE:shaodesk>
            $<TARGET_FILE:shaodesk-shell> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(overlay_exit_smoke PROPERTIES TIMEOUT 60)
        # A few of the gallery's popups and overlays through the GPU (Mesa's software OpenGL, in a
        # private compositor), where effects are drawn. Not under the sanitizers: now and then
        # Qt's Wayland platform leaks a surface of a preview that quits soon after showing.
        if(NOT CMAKE_CXX_FLAGS MATCHES "-fsanitize")
            add_test(NAME shell_gallery_gpu COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tests/shell_gallery.py
                ${PROJECT_SOURCE_DIR}/tools/shell_gallery.py ${PROJECT_BINARY_DIR} gpu
                bar launcher tray-menu notifications osd-volume cards switcher palette power-dialog)
            set_tests_properties(shell_gallery_gpu PROPERTIES TIMEOUT 120)
        endif()
        find_program(GRIM grim)
        # A missing grim is an empty argument, which the tests take as leaving out their
        # pixel checks (GRIM itself would be GRIM-NOTFOUND).
        set(GRIM_ARGUMENT "$<$<BOOL:${GRIM}>:${GRIM}>")
        add_test(NAME urgent_shell_smoke COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/urgent_shell_smoke.py $<TARGET_FILE:shaodesk>
            $<TARGET_FILE:shaodesk-shell> $<TARGET_FILE:wayland_probe> "${GRIM_ARGUMENT}")
        set_tests_properties(urgent_shell_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME popover_smoke COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/popover_smoke.py $<TARGET_FILE:shaodesk>
            $<TARGET_FILE:shaodesk-shell> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe>
            "${GRIM_ARGUMENT}")
        set_tests_properties(popover_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME touch_shell_smoke COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/touch_shell_smoke.py $<TARGET_FILE:shaodesk>
            $<TARGET_FILE:shaodesk-shell>)
        set_tests_properties(touch_shell_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME thumbnails_smoke COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/thumbnails_smoke.py $<TARGET_FILE:shaodesk>
            $<TARGET_FILE:shaodesk-shell> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:window_probe>
            $<TARGET_FILE:pointer_probe> "${GRIM_ARGUMENT}")
        set_tests_properties(thumbnails_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME taskbar_drag_smoke COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/taskbar_drag_smoke.py $<TARGET_FILE:shaodesk>
            $<TARGET_FILE:shaodesk-shell> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:window_probe>
            $<TARGET_FILE:pointer_probe>)
        set_tests_properties(taskbar_drag_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME taskbar_keyboard_smoke COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/taskbar_keyboard_smoke.py $<TARGET_FILE:shaodesk>
            $<TARGET_FILE:shaodesk-shell> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe>)
        set_tests_properties(taskbar_keyboard_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME switcher_pictures_smoke COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/switcher_pictures_smoke.py $<TARGET_FILE:shaodesk>
            $<TARGET_FILE:shaodesk-shell> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:window_probe>
            "${GRIM_ARGUMENT}")
        set_tests_properties(switcher_pictures_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME macos_shell_smoke COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/macos_shell_smoke.py $<TARGET_FILE:shaodesk>
            $<TARGET_FILE:shaodesk-shell> $<TARGET_FILE:wayland_probe> $<TARGET_FILE:pointer_probe>)
        set_tests_properties(macos_shell_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME volume_keys_shell_smoke COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/volume_keys_shell_smoke.py $<TARGET_FILE:shaodesk>
            $<TARGET_FILE:shaodesk-shell>)
        set_tests_properties(volume_keys_shell_smoke PROPERTIES TIMEOUT 90)
        add_test(NAME display_mode_shell_smoke COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/display_mode_shell_smoke.py $<TARGET_FILE:shaodesk>
            $<TARGET_FILE:shaodesk-shell> $<TARGET_FILE:pointer_probe>)
        set_tests_properties(display_mode_shell_smoke PROPERTIES TIMEOUT 90)
        if(SHAODESK_DBUS)
            add_test(NAME notifications_smoke COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tests/notifications_smoke.py $<TARGET_FILE:shaodesk>
                $<TARGET_FILE:shaodesk-shell> $<TARGET_FILE:notify_probe> $<TARGET_FILE:pointer_probe>)
            set_tests_properties(notifications_smoke PROPERTIES TIMEOUT 90)
        endif()
        if(SHAODESK_MEDIA_DBUS)
            add_test(NAME media_smoke COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tests/media_smoke.py $<TARGET_FILE:shaodesk>
                $<TARGET_FILE:shaodesk-shell> $<TARGET_FILE:mpris_probe>)
            set_tests_properties(media_smoke PROPERTIES TIMEOUT 90)
        endif()
        if(SHAODESK_TRAY_DBUS)
            add_test(NAME tray_smoke COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tests/tray_smoke.py $<TARGET_FILE:shaodesk>
                $<TARGET_FILE:shaodesk-shell> $<TARGET_FILE:tray_probe> $<TARGET_FILE:pointer_probe>)
            set_tests_properties(tray_smoke PROPERTIES TIMEOUT 90)
        endif()
    endif()
endif()
