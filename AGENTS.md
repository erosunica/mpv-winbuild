# Delivery instructions

The repository owner has authorized committing and pushing completed changes to
`erosunica/mpv-winbuild` as part of the requested work. Do not leave a completed
implementation only in the Codex cloud workspace.

- Validate the relevant changes, create a descriptive commit, and fetch the
  current remote branch before integrating. Preserve the owner's remote work.
- When `main` accepts direct writes, integrate the requested changes using a
  normal fast-forward push. Never force-push or bypass branch protection.
- If branch protection requires a pull request, push a feature branch and open
  the pull request. Report any integration step that remains pending.
- Deliver requested builds through GitHub release assets or Actions artifacts.
  Check the published files and provide GitHub links; local workspace paths are
  not published download links.
- If direct release uploads are unavailable, the `Publish CRT reference`
  workflow can publish verified ZIP files from a temporary
  `codex/crt-reference-assets/<delivery-id>` branch. Remove that temporary branch
  after confirming successful publication; keep the binary payload out of the
  history of `main`.
- If authentication, permissions, or platform restrictions prevent publication,
  preserve the local commit and report the specific blocker. Never claim that
  changes or builds are published before verifying the remote result.

These instructions apply to work requested by the owner; they do not authorize
unrelated repository changes or publications.
