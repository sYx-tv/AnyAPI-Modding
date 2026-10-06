# AnyAPI Manager 1.3.0

Settings now includes a manager update check. Choose **Check for updates**, then
**Update & restart** when a newer release is available. The manager downloads and
verifies its replacement EXE, keeps an exact backup, and restarts. Failed
replacement or restart restores the previous EXE.

Manager updates are independent of API and mod updates. Your installed DLLs,
enabled mod choices, preferences and saved data are retained. The bundled API and
offline guide remain the published 0.25.0 profile; newer compatible installed API
and mod receipts remain supported.

The download must belong to the official GitHub repository and match its release
feed's version, size and SHA-256. The updater also verifies the x64 EXE format and
managed assembly identity without executing the downloaded file during checks.

Validation: 93 fixture checks passed, including successful EXE replacement, exact
backup preservation, restart rollback, changed-file protection, downgrade
rejection, checksum/version rejection and waiting for the correct parent process.
The Settings page was rendered and inspected. These checks do not change the game.
