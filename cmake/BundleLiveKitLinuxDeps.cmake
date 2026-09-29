# Vendors LiveKit's own Linux runtime dependency chain (its bundled
# libcurl.so.4 plus everything THAT needs — OpenSSL, GnuTLS, Kerberos,
# LDAP, libssh, librtmp, nghttp2, idn2, psl, zstd, brotli, nettle, sasl2,
# p11-kit, tasn1, keyutils, libffi, ...) into a private, collision-free
# subdirectory of the install tree, so plain `cpack --generator RPM`
# packaging does not silently rely on the host Fedora system's own
# curl/openssl/gnutls/krb5/openldap packages, which LiveKit's prebuilt SDK
# was never built or tested against, and does not drop a second copy of
# those shared libraries at a path Fedora's own packages already own.
#
# Runs at INSTALL time (inside `cmake --install` / during `cpack`), after
# Qt's own qt_generate_deploy_app_script() output has already run, via a
# comparable install(SCRIPT ...) step wired up in CMakeLists.txt. Expects
# these variables to already be set (via install(CODE "set(...)") just
# before this script is include()d — see CMakeLists.txt):
#
#   LIVEKIT_SDK_LIB_DIR      - absolute path to the LiveKit SDK's own lib/
#                              directory in the BUILD tree.
#   SESSIO_INSTALL_LIBDIR    - absolute path to the install tree's main lib
#                              dir (where Qt's own deploy script already
#                              placed liblivekit.so/liblivekit_ffi.so and
#                              every Qt/qlementine/qt6keychain .so).
#   SESSIO_INSTALL_BINDIR    - absolute path to the install tree's bin dir
#                              (where the Sessio executable itself lives).
#   SESSIO_PRIVATE_LIBDIR_NAME - the private subdirectory name, "sessio".
#
# Does not redefine or rely on any variable not documented above.

find_program(PATCHELF_EXECUTABLE patchelf)
if(NOT PATCHELF_EXECUTABLE)
  message(WARNING
    "BundleLiveKitLinuxDeps: patchelf not found — LiveKit's own runtime "
    "dependency chain (bundled curl + OpenSSL/GnuTLS/Kerberos/LDAP/etc) was "
    "NOT vendored into a private directory. This install/package is not "
    "portable across Linux distros/versions for anything that exercises a "
    "LiveKit call. Install patchelf and re-run cmake --install/cpack.")
  return()
endif()

set(_private_dir "${SESSIO_INSTALL_LIBDIR}/${SESSIO_PRIVATE_LIBDIR_NAME}")
file(MAKE_DIRECTORY "${_private_dir}")

file(GLOB _lk_own_libs "${LIVEKIT_SDK_LIB_DIR}/*.so*")
if(NOT _lk_own_libs)
  message(FATAL_ERROR "BundleLiveKitLinuxDeps: no .so* files found under ${LIVEKIT_SDK_LIB_DIR}")
endif()

file(GET_RUNTIME_DEPENDENCIES
  LIBRARIES ${_lk_own_libs}
  RESOLVED_DEPENDENCIES_VAR _resolved
  UNRESOLVED_DEPENDENCIES_VAR _unresolved
)

if(_unresolved)
  message(WARNING
    "BundleLiveKitLinuxDeps: could not resolve these LiveKit runtime deps: "
    "${_unresolved} — the packaged app may fail at runtime on a machine "
    "without them installed system-wide.")
endif()

# Copy LiveKit's own libs (liblivekit.so, liblivekit_ffi.so, and anything
# else that ships directly alongside them in the SDK's lib/ dir, such as
# LiveKit's own bundled libcurl.so.4) into the private dir and give each an
# $ORIGIN rpath, so they resolve each other and their own further
# dependencies (patched in below) from within that one private directory.
foreach(_lib_path IN LISTS _lk_own_libs)
  get_filename_component(_lib_name "${_lib_path}" NAME)
  file(COPY "${_lib_path}" DESTINATION "${_private_dir}" FOLLOW_SYMLINK_CHAIN)
  set(_private_copy "${_private_dir}/${_lib_name}")
  set(_patch_ok FALSE)
  if(EXISTS "${_private_copy}")
    execute_process(
      COMMAND "${PATCHELF_EXECUTABLE}" --set-rpath "$ORIGIN" "${_private_copy}"
      RESULT_VARIABLE _rc
    )
    if(_rc EQUAL 0)
      set(_patch_ok TRUE)
    else()
      message(WARNING "BundleLiveKitLinuxDeps: patchelf --set-rpath failed on ${_lib_name}")
    endif()
  else()
    message(WARNING "BundleLiveKitLinuxDeps: expected private copy ${_private_copy} does not exist after file(COPY)")
  endif()

  # qt_generate_deploy_app_script() already placed an UNPATCHED copy of
  # LiveKit's own direct link libraries (liblivekit.so/liblivekit_ffi.so)
  # directly in SESSIO_INSTALL_LIBDIR (bare lib/), since they are Sessio's
  # own direct runtime deps. That bare copy's RUNPATH ($ORIGIN, i.e. bare
  # lib/ itself) can never see this script's private curl/openssl/etc
  # chain, and Sessio's rpath search order ($ORIGIN/../lib BEFORE
  # $ORIGIN/../lib/sessio, set below) means that bare, unpatched copy would
  # be found FIRST and loaded instead of the correctly-patched one above —
  # silently defeating this entire script (libcurl.so.4/libssl.so.3/etc
  # would still resolve against the host system). Delete the bare copy so
  # Sessio's rpath search falls through to the private, patched one
  # instead. Only do this once the private copy is confirmed to exist AND
  # be correctly patched, so a failure above can never leave the app with
  # NEITHER a working bare copy NOR a working private one.
  set(_bare_copy "${SESSIO_INSTALL_LIBDIR}/${_lib_name}")
  if(_patch_ok AND EXISTS "${_bare_copy}")
    file(REMOVE "${_bare_copy}")
  endif()
endforeach()

# Copy every further resolved runtime dependency (curl's own transitive
# closure: OpenSSL, GnuTLS, Kerberos, LDAP, etc) into the same private dir,
# also with an $ORIGIN rpath. Unlike liblivekit.so/liblivekit_ffi.so above,
# Qt's deploy script never placed any of these directly in
# SESSIO_INSTALL_LIBDIR (it only walks Sessio's own DIRECT link deps), so
# there is no bare copy to worry about here.
list(REMOVE_DUPLICATES _resolved)
foreach(_lib_path IN LISTS _resolved)
  get_filename_component(_lib_name "${_lib_path}" NAME)
  file(COPY "${_lib_path}" DESTINATION "${_private_dir}" FOLLOW_SYMLINK_CHAIN)
  execute_process(
    COMMAND "${PATCHELF_EXECUTABLE}" --set-rpath "$ORIGIN" "${_private_dir}/${_lib_name}"
    RESULT_VARIABLE _rc
  )
  if(NOT _rc EQUAL 0)
    message(WARNING "BundleLiveKitLinuxDeps: patchelf --set-rpath failed on ${_lib_name}")
  endif()
endforeach()

# Point the Sessio executable at the private dir too, ADDITIVELY — it
# already has an rpath entry (from Qt's deploy script) that finds Qt/
# qlementine/qt6keychain in SESSIO_INSTALL_LIBDIR; this must not be lost.
set(_sessio_bin "${SESSIO_INSTALL_BINDIR}/Sessio")
execute_process(
  COMMAND "${PATCHELF_EXECUTABLE}" --print-rpath "${_sessio_bin}"
  OUTPUT_VARIABLE _existing_rpath
  OUTPUT_STRIP_TRAILING_WHITESPACE
  RESULT_VARIABLE _rc
)
if(NOT _rc EQUAL 0)
  message(WARNING "BundleLiveKitLinuxDeps: patchelf --print-rpath failed on Sessio binary; leaving it unpatched")
else()
  # NOTE: deliberately NOT using a bare ${CMAKE_INSTALL_LIBDIR} here.
  # GNUInstallDirs is include()d at the top-level CMakeLists.txt at
  # CONFIGURE time, but this script runs INSIDE cmake_install.cmake at
  # actual install/package time, which is a separate script execution that
  # does not inherit configure-time variables unless they were explicitly
  # baked into an install(CODE "...") string (as SESSIO_INSTALL_LIBDIR/
  # SESSIO_INSTALL_BINDIR are). Verified empirically: a bare
  # ${CMAKE_INSTALL_LIBDIR} reference inside an install(CODE)/install(SCRIPT)
  # body evaluates to an EMPTY string at install time, which would silently
  # produce "$ORIGIN/../sessio" (missing the "lib" component) instead of
  # "$ORIGIN/../lib/sessio". Deriving the relative path from the two
  # absolute, already-resolved directories we WERE given avoids relying on
  # that variable at all.
  file(RELATIVE_PATH _private_dir_rel_to_bindir "${SESSIO_INSTALL_BINDIR}" "${_private_dir}")
  set(_new_rpath_entry "$ORIGIN/${_private_dir_rel_to_bindir}")
  if(_existing_rpath)
    set(_combined_rpath "${_existing_rpath}:${_new_rpath_entry}")
  else()
    set(_combined_rpath "${_new_rpath_entry}")
  endif()
  execute_process(
    COMMAND "${PATCHELF_EXECUTABLE}" --set-rpath "${_combined_rpath}" "${_sessio_bin}"
    RESULT_VARIABLE _rc
  )
  if(NOT _rc EQUAL 0)
    message(WARNING "BundleLiveKitLinuxDeps: patchelf --set-rpath failed on Sessio binary")
  endif()
endif()
