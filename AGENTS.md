# Agent Instructions

## Python

- When running Python commands in this repository, use the local virtual environment if it exists.
- On Windows/PowerShell, prefer `.\.venv\Scripts\python.exe` when present.
- On POSIX shells, prefer `./.venv/bin/python` when present.
- Fall back to `python` only when no local `.venv` interpreter exists.
- Prefer invoking the interpreter directly, for example `.\.venv\Scripts\python.exe -m pytest`, instead of relying on shell activation.

## Windows compatibility

- Maintain Windows 7 SP1 x64 as a separate compatibility target alongside the Windows 10/11 build. Follow `docs/windows-compatibility.md` for the current status and acceptance requirements.
- Keep the task, combat and device protocol behavior shared. Isolate platform and dependency differences instead of downgrading the modern runtime.
- When changing Windows APIs, capture, inference, Python dependencies or packaging, check both targets. Run `tools_release/audit_win7.py --check-optional-apis` for source changes.
- Do not claim Windows 7 support or publish a Windows 7 package until that complete package passes the Windows 7 acceptance checks. A successful modern build or static audit is insufficient.
- While the first Win7 port is incomplete, modern releases may continue with their existing requirements. Record pending legacy work explicitly; do not label a modern package as Win7-compatible.

## Build delivery and publication

- Build and validate packages locally first. After every completed build, provide the version and clickable absolute paths to the standard package, personal package and installed executable when available.
- After local validation, update the personal installation targeted by the desktop shortcut by default. Keep the shortcut in place, preserve the existing profile, and verify the installed version and profile before reporting delivery.
- Wait for an explicit publication command for that build before pushing to GitHub, creating or pushing tags, uploading release assets, or publishing a release. Approval for an earlier build does not authorize publication of a later build.
- Keep existing published versions unchanged unless the user explicitly requests a change or removal.

## Environment and cumulative updates

- Default public delivery is a fixed environment archive plus one cumulative application archive per version. Compare against the environment, not the preceding application release; users must be able to skip intermediate releases.
- Use `tools_release/prepare_environment.py`. Keep runtime dependencies and models immutable within an environment ID. Dependency changes require a new environment ID and archive; never replace a published environment in place.
- Keep the matching environment link first in the README download section. Version releases provide the cumulative application archive, source archive and checksums. Do not put the complete source tree or private profiles into application updates.
- Publish the immutable environment in its own Release. Keep its download link at the top of the README and in every matching application release. Do not mark an environment release as the application's Latest. Verify both public release asset hashes and download links before reporting publication complete.
- Test version jumps, reverted application files, removed managed files, mismatched environments, preserved profiles and rollback. Keep personal archives local. Publication still requires an explicit command for that build.
