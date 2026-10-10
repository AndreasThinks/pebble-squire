# Identity candidate close-out

Implementation and local polish are complete for review, not published.

Avery inspected the new companion and home layout, reran 101 JavaScript tests, 281 C sanitizer checks plus formatted-text tests, and Telegram bundle reproducibility and smoke checks. All passed. Credential-free and configured PBWs built for basalt, chalk, diorite, emery and gabbro. Authentication and transport source was not changed.

The icon generator was exercised; an 80px Store icon was added alongside the 144px icon. README separates Store installation from developer credentials, describes data flow and dictation-provider caveats, and preserves attribution. The release addendum distinguishes original sideload acceptance from the later user-confirmed Store install. Store copy remains a local draft.

Retained emulator design captures were visually inspected on emery, chalk and diorite. Final-build emulator installation failed on emery and timed out on chalk. The captures are not certified final-binary proof; their provenance is documented in docs/store/screenshots/README.md. Gabbro has a labelled deterministic preview only. The final candidate still requires real-watch visual acceptance and fresh release screenshots before publication.

Configured candidate: Squire-Plus-identity-candidate.pbw, retained outside Git under /home/avery/projects/squire-plus-artifacts/.
SHA-256: 94918b4cbc0e7900c4f986b6b1982adef0ec2d34dcc226da03e53c2d5ce44a0a

UUID and version remain those of 0.1.0 for this local candidate. Installing it replaces the installed Squire Plus build, not the original Squire. A version bump is needed for a future Store upload. Application credentials were injected only at build time; no personal session or bot token was embedded.

No commit, push, tag or Store upload was performed in this close-out. Duplicate notifications remain deferred. Telegram API terms applicability remains unresolved.
