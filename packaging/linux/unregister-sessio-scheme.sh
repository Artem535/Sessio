#!/bin/sh
# Post-uninstall step for the RPM package (wired via
# CPACK_RPM_POST_UNINSTALL_SCRIPT_FILE in CMakeLists.txt): refreshes the
# desktop database now that Sessio.desktop has been removed, so desktop
# environments notice the sessio:// association is gone. Deliberately does
# NOT call `xdg-mime default ...` — there is no reliable, safe way to
# "un-set" a MIME default without knowing what should replace it, and
# pointing it at a .desktop file that no longer exists (as register-
# sessio-scheme.sh's xdg-mime call would do if reused here) is worse than
# leaving the stale default in place. update-desktop-database is optional on
# a minimal system, so failure here must not fail the package uninstall.
set -e

if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database -q /usr/share/applications || true
fi

exit 0
