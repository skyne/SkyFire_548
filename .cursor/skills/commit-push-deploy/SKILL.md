---
name: commit-push-deploy
description: >-
  Commit, push, and watch the SkyFire realm deploy. Use when the user asks to
  commit and push, ship the current thread, or watch until the deployment is
  ready. Asks whether to commit all unstaged changes or just this thread's
  related changes, then pushes and reports when the images workflow deploy job
  succeeds.
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
git push -u origin HEAD
```

Push only the repo you committed. Do not push any other repo.

## 3. Watch the deployment

Realm images and the cluster roll come from `.github/workflows/images.yml` in SkyFire_548. A push starts that workflow only when all of these are true:

- the branch is `main` or `trunk`
- the commit touches `src/**`, `CMakeLists.txt`, `cmake/**`, `dep/**`, `deploy/docker/**`, or `.github/workflows/images.yml`
- for a push event, the Actions variable `SKYFIRE_DEPLOY` is `true` (otherwise the `deploy` job is skipped)

If the push cannot start a deploy, say that immediately and do not watch. Name the missing condition.

Otherwise find the run for the commit you just pushed:

```bash
gh run list --repo <owner>/<repo> --workflow images.yml --commit <full-sha> \
  --json databaseId,status,conclusion,headSha,url,displayTitle,event
```

The run can take a few minutes to appear. Poll the list until it does, then watch it to completion:

```bash
gh run watch <databaseId> --exit-status
gh run view <databaseId> --json conclusion,url,status,jobs
```

The `publish` job builds and pushes `ghcr.io/<owner>/skyfire-{authserver,worldserver,clustergateway}:548`. The `deploy` job then rolls namespace `skyfire`: `authserver`, `clustergateway`, and world roles `oldworld`, `outland`, `northrend`, `pandaria`, `bg`. A world pod that still has players is left on the old process; the workflow can still succeed.

If `gh run watch` returns while the run is still `in_progress` or `queued`, poll `gh run view` again until `conclusion` is set. Do not start a second deploy.

## 4. Notify

Tell the user in chat. Lead with the outcome.

**Deploy ready** — only when the `deploy` job `conclusion` is `success`. Include the commit subject, short sha, run URL, and that auth, the gateway, and the world roles were rolled. If the job log says a world was kept because players were connected, name that role.

**Not ready** — `publish` succeeded but `deploy` was skipped or failed, or `publish` failed. Include the run URL and the failing job. Do not call this ready.
