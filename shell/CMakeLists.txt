find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Network Qml Quick)
pkg_check_modules(GIO REQUIRED IMPORTED_TARGET gio-unix-2.0)
pkg_check_modules(SHELL_WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
# PulseAudio's client library (PipeWire serves it too) drives the panel's volume control.
pkg_check_modules(PULSE IMPORTED_TARGET libpulse)
if(NOT PULSE_FOUND)
    message(STATUS "libpulse not found: the panel's volume control is left out")
endif()
# The notification daemon speaks D-Bus; without QtDBus the shell builds without it and the panel
# leaves out the bell.
option(SHAODE_NOTIFICATIONS "Serve org.freedesktop.Notifications from the shell (needs Qt6 DBus)" ON)
set(SHAODE_DBUS OFF)
if(SHAODE_NOTIFICATIONS)
    find_package(Qt6 6.5 QUIET COMPONENTS DBus)
    if(Qt6DBus_FOUND)
        set(SHAODE_DBUS ON)
    else()
        message(STATUS "Qt6 DBus not found: the notification daemon is left out")
    endif()
endif()
if(NOT SHAODE_SHELL_PREVIEW_ONLY)
    find_package(LayerShellQt 6.6 QUIET)
    if(NOT LayerShellQt_FOUND)
        message(FATAL_ERROR "The desktop shell requires LayerShellQt 6.6+. Install layer-shell-qt, or use -DSHAODE_BUILD_SHELL=OFF for the compositor or -DSHAODE_SHELL_PREVIEW_ONLY=ON for UI development.")
    endif()
endif()

set(SHAODE_NOTIFY_SOURCES notifications.cpp notifications.hpp notification_images.cpp
    notification_images.hpp osd.cpp osd.hpp backlight.cpp backlight.hpp)
# The QML is compiled ahead of time into the binary (qmlcachegen), so starting the shell parses
# and compiles none of it. It lives at qrc:/shell/ShaoDeShell/*.qml.
qt_add_library(shaode_shell_qml STATIC)
qt_add_qml_module(shaode_shell_qml URI ShaoDeShell VERSION 1.0 RESOURCE_PREFIX /shell
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/ShaoDeShell
    NO_PLUGIN NO_GENERATE_QMLTYPES
    QML_FILES Panel.qml Icon.qml Desktop.qml Switcher.qml Palette.qml Overview.qml NotificationCards.qml Osd.qml ConfigError.qml NotificationBell.qml NotificationHistory.qml)
add_executable(shaode-shell main.cpp ${SHAODE_NOTIFY_SOURCES} controller.cpp controller.hpp palette.cpp palette.hpp fuzzy.cpp fuzzy.hpp task_model.cpp task_model.hpp
    task_filter.cpp task_filter.hpp icons.cpp icons.hpp wallpapers.cpp wallpapers.hpp view.cpp view.hpp audio.cpp audio.hpp system_status.cpp system_status.hpp
    ${PROJECT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-protocol.c
    ${PROJECT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h)
add_dependencies(shaode-shell shaode_wlr_client_protocols)
set_target_properties(shaode-shell PROPERTIES AUTOMOC ON RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
target_include_directories(shaode-shell PRIVATE ${PROJECT_BINARY_DIR})
target_link_libraries(shaode-shell PRIVATE shaode_config Qt6::Core Qt6::Gui Qt6::Network Qt6::Qml Qt6::Quick
    PkgConfig::GIO PkgConfig::SHELL_WAYLAND)
target_compile_definitions(shaode-shell PRIVATE QT_NO_KEYWORDS)
if(SHAODE_DBUS)
    target_sources(shaode-shell PRIVATE notification_service.cpp notification_service.hpp)
    target_link_libraries(shaode-shell PRIVATE Qt6::DBus)
    target_compile_definitions(shaode-shell PRIVATE SHAODE_DBUS=1)
endif()
if(PULSE_FOUND)
    target_sources(shaode-shell PRIVATE pulse_audio.cpp)
    target_link_libraries(shaode-shell PRIVATE PkgConfig::PULSE)
    target_compile_definitions(shaode-shell PRIVATE SHAODE_PULSE=1)
endif()
target_compile_options(shaode-shell PRIVATE -Wall -Wextra -Wpedantic)
if(SHAODE_SHELL_PREVIEW_ONLY)
    target_compile_definitions(shaode-shell PRIVATE SHAODE_LAYER_SHELL=0)
else()
    target_link_libraries(shaode-shell PRIVATE LayerShellQt::Interface)
    target_compile_definitions(shaode-shell PRIVATE SHAODE_LAYER_SHELL=1)
    install(TARGETS shaode-shell RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
target_link_libraries(shaode-shell PRIVATE shaode_shell_qml)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
    add_executable(shell_ui_test ${PROJECT_SOURCE_DIR}/tests/shell_ui_test.cpp
        ${SHAODE_NOTIFY_SOURCES} controller.cpp controller.hpp palette.cpp palette.hpp fuzzy.cpp fuzzy.hpp view.cpp view.hpp task_model.cpp task_model.hpp
        task_filter.cpp task_filter.hpp icons.cpp icons.hpp wallpapers.cpp wallpapers.hpp audio.cpp audio.hpp system_status.cpp system_status.hpp
        ${PROJECT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-protocol.c
        ${PROJECT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h)
    set_target_properties(shell_ui_test PROPERTIES AUTOMOC ON)
    target_include_directories(shell_ui_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
    target_compile_definitions(shell_ui_test PRIVATE QT_NO_KEYWORDS SHAODE_LAYER_SHELL=0)
    target_compile_options(shell_ui_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(shell_ui_test PRIVATE shaode_config Qt6::Test Qt6::Network Qt6::Quick Qt6::Qml
        PkgConfig::GIO PkgConfig::SHELL_WAYLAND)
    target_link_libraries(shell_ui_test PRIVATE shaode_shell_qml)
    add_test(NAME shell_ui COMMAND shell_ui_test ${CMAKE_COMMAND})
    set_tests_properties(shell_ui PROPERTIES TIMEOUT 90
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software")
    add_executable(task_filter_test ${PROJECT_SOURCE_DIR}/tests/task_filter_test.cpp
        ${SHAODE_NOTIFY_SOURCES} controller.cpp controller.hpp palette.cpp palette.hpp fuzzy.cpp fuzzy.hpp view.cpp view.hpp task_model.cpp task_model.hpp
        task_filter.cpp task_filter.hpp icons.cpp icons.hpp wallpapers.cpp wallpapers.hpp audio.cpp audio.hpp system_status.cpp system_status.hpp
        ${PROJECT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-protocol.c
        ${PROJECT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h)
    set_target_properties(task_filter_test PROPERTIES AUTOMOC ON)
    target_include_directories(task_filter_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
    target_compile_definitions(task_filter_test PRIVATE QT_NO_KEYWORDS SHAODE_LAYER_SHELL=0)
    target_compile_options(task_filter_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(task_filter_test PRIVATE shaode_config Qt6::Test Qt6::Network Qt6::Quick Qt6::Qml
        PkgConfig::GIO PkgConfig::SHELL_WAYLAND)
    target_link_libraries(task_filter_test PRIVATE shaode_shell_qml)
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
    add_executable(task_model_test ${PROJECT_SOURCE_DIR}/tests/task_model_test.cpp
        task_model.cpp task_model.hpp
        ${PROJECT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-protocol.c
        ${PROJECT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h)
    set_target_properties(task_model_test PROPERTIES AUTOMOC ON)
    target_include_directories(task_model_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
    target_compile_definitions(task_model_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(task_model_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(task_model_test PRIVATE Qt6::Core PkgConfig::SHELL_WAYLAND)
    add_test(NAME shell_tasks COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/compositor_smoke.py $<TARGET_FILE:shaode>
        $<TARGET_FILE:wayland_probe> ${PROJECT_SOURCE_DIR}/config/init.lua
        $<TARGET_FILE:task_model_test>)
    set_tests_properties(shell_tasks PROPERTIES TIMEOUT 60)
    add_executable(notifications_test ${PROJECT_SOURCE_DIR}/tests/notifications_test.cpp
        notifications.cpp notifications.hpp)
    set_target_properties(notifications_test PROPERTIES AUTOMOC ON)
    target_include_directories(notifications_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(notifications_test PRIVATE QT_NO_KEYWORDS)
    target_compile_options(notifications_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(notifications_test PRIVATE shaode_config Qt6::Gui Qt6::Test)
    add_test(NAME shell_notifications COMMAND notifications_test)
    set_tests_properties(shell_notifications PROPERTIES TIMEOUT 90
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    if(SHAODE_DBUS)
        add_executable(notifications_dbus_test ${PROJECT_SOURCE_DIR}/tests/notifications_dbus_test.cpp
            notifications.cpp notifications.hpp notification_service.cpp notification_service.hpp)
        set_target_properties(notifications_dbus_test PROPERTIES AUTOMOC ON)
        target_include_directories(notifications_dbus_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(notifications_dbus_test PRIVATE QT_NO_KEYWORDS)
        target_compile_options(notifications_dbus_test PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(notifications_dbus_test PRIVATE shaode_config Qt6::Gui Qt6::DBus Qt6::Test)
        # The test talks to a bus of its own; the environment's is made unusable so nothing can
        # reach the real one.
        add_executable(notify_probe ${PROJECT_SOURCE_DIR}/tests/notify_probe.cpp)
        set_target_properties(notify_probe PROPERTIES AUTOMOC ON RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
        target_link_libraries(notify_probe PRIVATE Qt6::Core Qt6::DBus)
        add_test(NAME shell_notifications_dbus COMMAND notifications_dbus_test)
        set_tests_properties(shell_notifications_dbus PROPERTIES TIMEOUT 60
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;DBUS_SESSION_BUS_ADDRESS=disabled:")
    endif()
    add_executable(osd_test ${PROJECT_SOURCE_DIR}/tests/osd_test.cpp
        ${SHAODE_NOTIFY_SOURCES} controller.cpp controller.hpp palette.cpp palette.hpp fuzzy.cpp fuzzy.hpp view.cpp view.hpp task_model.cpp task_model.hpp
        task_filter.cpp task_filter.hpp icons.cpp icons.hpp wallpapers.cpp wallpapers.hpp audio.cpp audio.hpp system_status.cpp system_status.hpp
        ${PROJECT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-protocol.c
        ${PROJECT_BINARY_DIR}/wlr-foreign-toplevel-management-unstable-v1-client-protocol.h)
    set_target_properties(osd_test PROPERTIES AUTOMOC ON)
    target_include_directories(osd_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_BINARY_DIR})
    target_compile_definitions(osd_test PRIVATE QT_NO_KEYWORDS SHAODE_LAYER_SHELL=0)
    target_compile_options(osd_test PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(osd_test PRIVATE shaode_config Qt6::Test Qt6::Network Qt6::Quick Qt6::Qml
        PkgConfig::GIO PkgConfig::SHELL_WAYLAND)
    target_link_libraries(osd_test PRIVATE shaode_shell_qml)
    add_test(NAME shell_osd COMMAND osd_test)
    set_tests_properties(shell_osd PROPERTIES TIMEOUT 30
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
    add_test(NAME shell_preview COMMAND ${Python3_EXECUTABLE}
        ${PROJECT_SOURCE_DIR}/tests/shell_preview.py $<TARGET_FILE:shaode-shell>
        ${PROJECT_SOURCE_DIR}/config/init.lua)
    set_tests_properties(shell_preview PROPERTIES TIMEOUT 90)
    if(NOT SHAODE_SHELL_PREVIEW_ONLY)
        add_test(NAME shell_integration COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/shell_integration.py $<TARGET_FILE:shaode>
            $<TARGET_FILE:shaode-shell> $<TARGET_FILE:wayland_probe>
            ${PROJECT_SOURCE_DIR}/config/init.lua)
        set_tests_properties(shell_integration PROPERTIES TIMEOUT 90)
        add_test(NAME overview_shell_smoke COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/overview_shell_smoke.py $<TARGET_FILE:shaode>
            $<TARGET_FILE:shaode-shell> $<TARGET_FILE:wayland_probe>)
        set_tests_properties(overview_shell_smoke PROPERTIES TIMEOUT 60)
        find_program(GRIM grim)
        add_test(NAME urgent_shell_smoke COMMAND ${Python3_EXECUTABLE}
            ${PROJECT_SOURCE_DIR}/tests/urgent_shell_smoke.py $<TARGET_FILE:shaode>
            $<TARGET_FILE:shaode-shell> $<TARGET_FILE:wayland_probe> "${GRIM}")
        set_tests_properties(urgent_shell_smoke PROPERTIES TIMEOUT 90)
        if(SHAODE_DBUS)
            add_test(NAME notifications_smoke COMMAND ${Python3_EXECUTABLE}
                ${PROJECT_SOURCE_DIR}/tests/notifications_smoke.py $<TARGET_FILE:shaode>
                $<TARGET_FILE:shaode-shell> $<TARGET_FILE:notify_probe> $<TARGET_FILE:pointer_probe>)
            set_tests_properties(notifications_smoke PROPERTIES TIMEOUT 90)
        endif()
    endif()
endif()
