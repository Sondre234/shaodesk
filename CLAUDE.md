# Git

- Commit without asking. Commit often, one small, self-contained change per commit, so any
  step can be rolled back on its own. Commit as soon as a change builds (and its tests pass,
  when there are any), not at the end of a session.
- Work on a branch off `main` (`feat/…`, `fix/…`, `chore/…`, `docs/…`) and merge it into `main`
  with `git merge --no-ff` (default message, "Merge branch '<name>'") once the change is done
  and working. Don't push unless asked.
- Stage only the files you changed for that commit (`git add <paths>`, never `git add -A`), so
  unrelated work in progress in the tree stays out of it.
- Never amend, rebase or reset commits that are already pushed.
