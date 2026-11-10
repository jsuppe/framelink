# Local environment and committed placeholders

Evidence and scripts often need this machine's paths, the Windows test host and
its user. Those values live in `local.env` at the checkout root, which is
gitignored, and a git filter keeps them out of commits:

| `local.env` key | placeholder in commits | example |
|---|---|---|
| `FL_LINUX_HOME` | `<linux-home>` | `/home/you` |
| `FL_LINUX_HOST` | `<linux-host>` | `linux-box` |
| `FL_WIN_HOME`   | `<win-home>`   | `C:\Users\you`, matched in `\`, `\\` and `/` spellings |
| `FL_WIN_USER`   | `<win-user>`   | `you`, as in `ssh you@windows-box` |
| `FL_WIN_HOST`   | `<win-host>`   | `windows-box` |

Set up once per clone (the filter config is shared by its worktrees):

```
python3 tools/setup_local_env.py      # creates local.env from local.env.example
$EDITOR local.env                     # this machine's values
python3 tools/setup_local_env.py --check
```

On commit (`clean`), local values become placeholders; on checkout (`smudge`),
placeholders become this machine's values, so archived scripts such as
`docs/m6/*/run.py` run unchanged. Without `local.env` the filter passes content
through, so `python3 tools/check_local_identity.py` runs in CI and fails on any
`/home/<name>` or `C:\Users\<name>` path. Run it with `--cached` before
committing to also check the exact `local.env` user and host names.

Never commit a whole-machine snapshot (process lists, environment dumps,
registry caches); keep those beside the evidence outside the repository.
