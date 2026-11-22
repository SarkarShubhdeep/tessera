# How work moves

Tessera is built in the open, and its history is part of the documentation: why each choice was made,
what changed, and how it was checked. So every change follows the same path.

1. **Issue.** Every piece of work starts as an issue: the goal, the options considered, the tasks as a
   checklist, and what "done" means. A large piece is split into sub-issues.
2. **Decision.** A significant choice (a provider, a model, a hardware approach) also gets an entry in
   [docs/decisions.md](docs/decisions.md).
3. **Branch.** Work happens on a short-named branch off an up-to-date `main`. Nothing is committed to
   `main` directly.
4. **Review before flashing.** Anything that changes what runs on the board gets a written review in
   `docs/` first (which pins it drives, what it writes, what leaves the device), and is flashed only after
   the owner approves it. Results from the board go back into the same review.
5. **Pull request.** The branch becomes a pull request that links its issue. The last PR for an issue says
   `Closes #N`. PRs are merged into `main` one at a time, after review.
6. **Checklist.** The issue's checklist is ticked as each task lands, so the issue always shows where the
   work stands.

## Secrets

API keys, Wi-Fi passwords and network names never go into the repo, an issue, a PR or a log. Keys are
typed into the device by its owner (see [review 05](docs/review-05-voice.md)). Board backups contain
saved settings and stay out of the repo.
