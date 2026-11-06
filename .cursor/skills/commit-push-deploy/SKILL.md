---
name: commit-push-deploy
description: >-
  Commit, push, and watch the SkyFire realm deploy. Use when the user asks to
  commit and push, ship the current thread, or watch until the deployment is
  ready. Asks whether to commit all unstaged changes or just this thread's
  related changes, turns SKYFIRE_DEPLOY on for that push, then pushes and
  reports when the build workflow's roll step succeeds. Turns the variable
  off again after the run finishes.
---

# Commit, push, and watch the deploy

Do not stage, commit, or push until the user answers the scope question below.

## 1. Ask what to commit

Inspect the repo that contains the work (`git status`, `git diff`, `git log -8 --oneline`). List two sets:

- **All unstaged changes** — every unstaged modification and untracked file in that repo. Leave out secrets (`.env`, credentials, keys).
- **This thread only** — files this conversation created or edited. If a file was also edited outside this thread, still include it only when this thread changed it.

Ask with the AskQuestion tool, one question, these options, before any git write:

- All unstaged changes
- Just this thread's related changes

Stop if the user dismisses the question. If the chosen set is empty, say so and stop. Do not commit the other set.

If the index already contains paths outside the chosen set, unstage those paths with `git restore --staged -- <path>` so the working tree stays intact. Then stage only the chosen set.

## 2. Commit and push

Follow the repository commit rules: no git config changes, no `--no-verify`, no force push, no empty commit. Pass the message as a heredoc. One or two sentences on why.

```bash
git commit -m "$(cat <<'EOF'
Commit message here.

EOF
)"
git status
```

Push only the repo you committed. Do not push any other repo.

Before the push, turn on the repository variable that lets this push roll the cluster. `vars.SKYFIRE_DEPLOY` is read when the roll steps run, which is after the compile, so set it before `git push` and leave it `true` until that run has finished the roll steps.

```bash
gh variable set SKYFIRE_DEPLOY --repo <owner>/<repo> --body true
gh variable list --repo <owner>/<repo>
git push -u origin HEAD
```

A push still does not roll when the branch is not `main` or `trunk`, or when the commit only changes `sql/**`. In those cases do not set the variable to `true`. Say which condition is missing, and do not watch.

After the run completes, or if the watch stops because the run failed, set the variable back to `false` so the next push only publishes images:

```bash
gh variable set SKYFIRE_DEPLOY --repo <owner>/<repo> --body false
```

Do that even when the roll failed. Another push that starts while the variable is still `true` will roll as well, including a run that has not reached its roll steps yet.

## 3. Watch the deployment

Realm images and the cluster roll come from `.github/workflows/ubuntu.yml` in SkyFire_548. One `build` job compiles once, publishes images from that build, then rolls the cluster. A push starts that workflow when the branch is `main` or `trunk` and the commit changes something other than `sql/**`. The roll steps run only while `SKYFIRE_DEPLOY` is `true`.

Find the run for the commit you just pushed:

```bash
gh run list --repo <owner>/<repo> --workflow ubuntu.yml --commit <full-sha> \
  --json databaseId,status,conclusion,headSha,url,displayTitle,event
```

The run can take a few minutes to appear. Poll the list until it does, then watch it to completion:

```bash
gh run watch <databaseId> --exit-status
gh run view <databaseId> --json conclusion,url,status,jobs
```

The job builds with PCH, pushes `ghcr.io/<owner>/skyfire-{authserver,worldserver,clustergateway}:<12-char sha>` from that build, then the `Roll the realm` step rolls namespace `skyfire`: `authserver`, `clustergateway`, and world roles `oldworld`, `outland`, `northrend`, `pandaria`, `bg`. A world pod that still has players is left on the old process; the workflow can still succeed.

If `gh run watch` returns while the run is still `in_progress` or `queued`, poll `gh run view` again until `conclusion` is set. Do not start a second deploy.

Then set `SKYFIRE_DEPLOY` back to `false`, as in section 2. Include that in the report.

## 4. Notify

Tell the user in chat. Lead with the outcome.

**Deploy ready** — only when the `build` job `conclusion` is `success` and its `Roll the realm` step `conclusion` is `success`. Include the commit subject, short sha, run URL, and that auth, the gateway, and the world roles were rolled. If the job log says a world was kept because players were connected, name that role.

**Not ready** — the job failed, or it succeeded while `Roll the realm` was skipped. Include the run URL and the failing step when there is one. Do not call this ready.
