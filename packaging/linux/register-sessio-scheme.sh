#!/bin/sh
# Post-install step for the RPM package (wired via CPACK_RPM_POST_INSTALL_SCRIPT_FILE
# in CMakeLists.txt): registers Sessio.desktop as the default handler for the
# sessio:// URL scheme declared in its MimeType= line, and refreshes the
# desktop database so the new MimeType is picked up immediately. Both tools
# are optional on a minimal system, so failures here must not fail the
# package install/uninstall.
set -e

if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database -q /usr/share/applications || true
fi

if command -v xdg-mime >/dev/null 2>&1; then
  xdg-mime default Sessio.desktop x-scheme-handler/sessio || true
fi

exit 0
