# Private Development Workflow

This checkout is used for private work on `LeMegaGeek/bgobs`. The public project remains available for comparison, but
local work should not be pushed there.

## Remotes

Expected remote layout:

```text
origin   git@github.com:LeMegaGeek/bgobs.git
upstream https://github.com/royshil/obs-backgroundremoval.git
```

`origin` is the private repository. `upstream` is the public source repository and should be used for fetch, diff, and
rebase operations only.

The local `upstream` push URL is intentionally disabled:

```bash
git remote set-url --push upstream DISABLED
```

## Daily Flow

```bash
git fetch upstream
git status --short --branch
git diff --stat
```

Before creating any commit:

```bash
cmake --build build/local-obs --parallel
ctest --test-dir build/local-obs --output-on-failure
.venv/bin/reuse lint
git diff --check
```

## Upstream Contribution Gate

Before anything is sent to `royshil/obs-backgroundremoval`, verify the contribution manually against
`CONTRIBUTING.md`:

- Understand and be able to explain every changed line.
- Review, edit, and own all draft material before submission.
- Add proper SPDX copyright lines for new files.
- Use DCO sign-off and commit signing with `git commit -s -S`.
- Push only after an explicit decision to prepare an upstream PR.

---

> SPDX-FileCopyrightText: 2026 LeMegaGeek <d.github@chey.net>  
>
> SPDX-License-Identifier: GPL-3.0-or-later  
