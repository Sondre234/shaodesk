# Architecture

How the code is laid out, and which files a change usually touches. [CONTRIBUTING](../CONTRIBUTING.md)
covers branches, building, testing and committing; [features.md](features.md) describes behavior.

## Processes

- **`shaodesk`** (`src/main.cpp`) is the compositor. `main.cpp` loads the Lua configuration,
  watches it, runs scripts (screenshots, spawned programs) and answers the compositor's
  questions through a table of callbacks (`struct sh_callbacks` in
  `include/shaodesk/backend.h`). Then it hands over to `sh_run`, the C compositor in
  `src/compositor/`. `shaodesk msg ...` is the same binary acting as a control client.
- **`shaodesk-shell`** (`shell/`) is the Qt Quick shell: panels, launcher, notifications, tray, OSD,
  and the text of the switcher and overview. It is an ordinary layer-shell client that
  connects to the control socket with `subscribe` and gets state lines and events.

## Layers

| Where | Language | What |
| --- | --- | --- |
| `src/compositor/` | C | The compositor proper, on wlroots. Shares one private header, `server.h`. |
| `src/*.c`, `include/shaodesk/*.h` | C | Pieces the compositor uses that stand on their own: animations, decorations and tab strips (pixels), effect arithmetic, overview thumbnails, session files, the logind client. Several are unit tested. |
| `src/config.cpp`, `src/config_schema.cpp` | C++ | The Lua configuration: parsing, validation, the action table and the settings schema. |
| `src/tiling.cpp`, `src/layout.cpp`, `src/window_placement.cpp`, `src/overview_layout.cpp` | C++ | Pure geometry (tiling layouts, snapping, placement, the overview grid), with a C interface in `backend.h` and unit tests. |
| `src/import/` | C++ | `shaodesk import` from Hyprland and Waybar. |
| `shell/` | C++/QML | The shell. |

Keep geometry and arithmetic out of `src/compositor/` where you can: a pure function in its own
file can be unit tested without starting a compositor.

## The compositor (`src/compositor/`)

Each file begins with a comment saying what it covers; `head -4 src/compositor/*.c` shows them
all. In short:

| File | Covers |
| --- | --- |
| `server.c` | Startup (creating every wlroots global and listener), shutdown, config reload, signals. |
| `server.h` | The shared types (`sh_server`, `sh_output`, `sh_toplevel`, ...) and, under a `/* file.c */` heading, every function one file calls in another. |
| `actions.c` | `run_action`: one `case` per action, handing it to the module that does it. |
| `control.c` | The control socket: reading requests, commands that are not actions, subscribers and shell events. |
| `query.c` | `shaodesk msg get ...`: one function per query, and the table that names them. |
| `input.c` | Keyboards, key bindings, pointers' libinput settings, virtual devices, selection and drag-and-drop. |
| `keymap.c` | The keymap from the keyboard settings, given to every keyboard but virtual ones. |
| `cursor.c` | What is under the pointer, focus on hover, button bindings, scrolling, the cursor image. |
| `grab.c` | Moving and resizing with the pointer, magnetic edges, dropping. |
| `focus.c` | Keyboard focus and urgent windows. |
| `toplevel.c` | Windows: xdg-shell toplevels and popups, opening by window rules, maximize, fullscreen, minimize. |
| `xwayland.c` | X11 windows and the XWM waker. |
| `frame.c` | Borders, opacity, rounded corners, window controls, tab strips. |
| `placement.c` | Snapping, maximizing, reflowing, moving and resizing by keyboard. |
| `tiling.c` | Glue between windows and the layouts in `src/tiling.cpp`. |
| `workspace.c` | Workspaces per output, sticky windows. |
| `output.c`, `output_moves.c` | Monitors and their configuration; windows and workspaces moving between outputs. |
| `layer_shell.c` | Panels and other layer surfaces. |
| `group.c`, `scratchpad.c`, `swallow.c`, `switcher.c`, `overview.c`, `session.c` | One feature each. |
| `effects.c` | Dimming, peek, night light, magnifier, hot corners. |
| `lock.c` | Session lock and idle/sleep inhibitors. |
| `power.c` | The power actions: suspend, hibernate, reboot and power off through logind (`src/login1.c`), locking first, closing windows first, log out. |
| `foreign_toplevel.c` | Window lists for taskbars and single-window capture. |

A function used by one file is `static`; one used by several is declared in `server.h` under
the file that defines it. The build warns (`-Wmissing-prototypes`) about one that is neither.
Code that only exists with XWayland is inside `#if WLR_HAS_XWAYLAND`.

## Recipes

### A new binding action

1. Add `SH_<NAME>` to `enum sh_action` in `include/shaodesk/backend.h`.
2. Name it in `action_table` in `src/config.cpp`. That makes it bindable, usable from
   `shaodesk msg` and hot corners, and listed in the configuration reference.
3. If it takes an argument, parse it in the `command` callback in `src/main.cpp` (the
   `action_takes_*` helpers in `config.cpp` say which kinds exist); otherwise it is refused
   with "takes no argument". One that starts a program goes through the `launch` callback
   there, as `spawn` and `terminal` do, so a failure reaches the panel.
4. Add a `case` to `run_action` in `src/compositor/actions.c` that calls the module doing the
   work. If it acts on the window under the pointer when bound to a button, list it in
   `action_targets_window` in `cursor.c`.
5. Mention it in `README.md` or `docs/features.md`. The `docs_consistency` test fails until
   you do.

### A new setting

See "Lua settings" in [CONTRIBUTING](../CONTRIBUTING.md#lua-settings): an entry in
`src/config_schema.cpp`, parsing in `src/config.cpp`, a field in `struct sh_settings`
(`backend.h`), then regenerating `docs/config-reference.md`. The compositor reads settings
through `server_settings(server)`, and reacts to a reload in `reload_config` (`server.c`).

### A new query (`shaodesk msg get NAME`)

Write `static void get_<name>(struct sh_server *server, int fd, const char *arguments)` in
`src/compositor/query.c` and add it to `queries[]`. Reply with `control_reply(fd, "ok\n")`
followed by tab-separated lines, or a line starting `error: `. `arguments` is NULL unless the
table entry accepts them. Queries answer while the session is locked, so they must not change
anything. Tests use queries to see the compositor's state, so a feature that is hard to test
often wants one.

### A new control command that is not an action

Handle it in `control_handle` in `src/compositor/control.c`, after the session-lock check,
following `dnd` or `osd`.

### A new Wayland protocol or global

Create the global in `sh_run` in `server.c`, keep its pointer and `wl_listener`s in
`struct sh_server` (`server.h`), and put the handlers in the module the protocol belongs to,
declaring the ones `server.c` connects in `server.h`. Add the `#include` for its wlroots
header to `server.h`.

### Telling the shell something

`send_event` (`control.c`) sends a line to every subscriber; `send_shell_line` and
`request_shell` are the shell-specific forms, and `report_failure` logs a failure the user no
longer waits on and shows it across the panel (`power-error`, `spawn-error`). The shell reads
them in `shell/controller.cpp`.

### A new test

- A pure function: a unit test next to the others in `tests/*_tests.c(pp)`, registered with
  `add_executable` and `add_test` in `CMakeLists.txt`.
- Compositor behavior: a smoke test, `tests/<name>_smoke.py`. Copy a short one such as
  `sticky_smoke.py`. It starts a headless compositor with the pixman renderer in a temporary
  `XDG_RUNTIME_DIR`, opens windows with `wayland_probe` (or `x11_probe`), drives it with
  `shaodesk msg`, and reads the state back with `get` queries. Wait with
  `harness.wait_for`, never with a fixed sleep. Register it with `add_test` and a `TIMEOUT`
  under `SHAODESK_BUILD_COMPOSITOR` in `CMakeLists.txt`. Keep the temporary directory's prefix
  to 26 characters or fewer: the control socket goes in it, a Unix socket's path is limited to
  about 107 bytes, and a Gentoo package build runs the tests in a `TMPDIR` of 43 characters or
  more. Under `--headless`, `shaodesk msg headless_output` and `headless_keyboard` plug in
  outputs and keyboards (`headless_keyboard key NAME CODE press` types on one; see
  `keymap_smoke.py`), and `wayland_probe --keymap` prints the keymap an application gets.

## A fast loop

```sh
cmake --build build --target shaodesk          # only the compositor
ctest --test-dir build -j8 -R sticky           # only the tests you are working on
ctest --test-dir build -j8                     # everything, about 25 seconds
tools/check-all.sh                             # plus the sanitizer build, before merging
```

`build/compile_commands.json` is there for clangd. `shaodesk --headless` with
`WLR_RENDERER=pixman` runs the compositor with no display, as the tests do.
