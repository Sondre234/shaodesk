# Git workflow

shaosde keeps one long-lived branch, `master`. Work is committed straight to it in
small steps; there are no `develop` or feature branches to merge.

## Working in a worktree

Do each task in its own git worktree, so several pieces of work can run side by side
without disturbing one another or the main checkout:

```sh
git worktree add ../shaosde-wt/<name> -b wt/<name> master
cd ../shaosde-wt/<name>
```

Build in the worktree with its own build directory:

```sh
cmake -S . -B build -G Ninja -DSHAODE_BUILD_COMPOSITOR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`tools/check-all.sh` does that and then repeats the suite in a second build with
AddressSanitizer, UndefinedBehaviorSanitizer and leak detection (`build-asan`); run it before
larger pushes. `--quick` skips the sanitizer pass, `--sanitize` runs only it, and
`--repeat 20` runs every test up to 20 times, which is how flaky tests are found. A test
that only fails on a loaded machine is a bug in the test: wait for the condition through
`tests/harness.py` instead of sleeping.

`cmake --build build --target all_qmllint` lints the shell's QML (`shell/.qmllint.ini` says which
checks the context-property design makes meaningless); it should print nothing.

Remove the worktree when the work is done:

```sh
git worktree remove ../shaosde-wt/<name>
git branch -d wt/<name>
```

## Committing

Commit small and often. The build and `ctest` must pass before you push, and each
commit should leave the tree in a working state. Write a short imperative subject
line ("Group an application's windows into one taskbar button"), and mention the
GitHub issue when there is one.

Add a line to [CHANGELOG.md](CHANGELOG.md) for changes users will notice, and keep
[README.md](README.md) in step with behavior changes (settings, bindings, messages).

## Publishing

Every commit goes to `master` as soon as it is ready:

```sh
git fetch origin
git rebase origin/master
git push origin HEAD:master
```

If the push is rejected, another worktree pushed first: fetch, rebase, and try again.
Never force-push. Rebase often when several people or agents work at once, and keep
edits to shared files such as `README.md` small and targeted so rebases stay clean.

## Testing on hardware

The automated tests run headless. Anything that needs a real display, GPU, or input
device (`--session` on a TTY, multiple monitors, real applications) has to be checked
by hand; record what was and was not checked in [docs/verification.md](docs/verification.md).

## Keeping private details out

`origin` (github.com/Sondre234/shaosde) is public. Do not commit LAN addresses, host
keys, passwords, home-directory paths, or machine-specific access details. Personal
key bindings and themes belong in `~/.config/shaode`, not in the repository.

## Lua settings

The settings the configuration accepts are listed in `src/config_schema.cpp`; the parser
takes its allowed names from that list, so a new setting or table key needs an entry there
(type, default, an example value, its range) as well as code in `src/config.cpp`. Then
regenerate the reference with `SHAODE_UPDATE_DOCS=1 ctest --test-dir build -R config` and
commit `docs/config-reference.md`; the `config_diagnostics` test fails while they differ. A
new action name goes in the action table in `src/config.cpp`, which the reference reads.
