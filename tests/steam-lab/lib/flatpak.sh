# shellcheck shell=bash
#
# Building, installing and running the Flatpak.
#
# This tier runs on the host by default rather than in a container, and the
# reason is worth writing down because it looks like the lazy choice and is not:
#
#   Flatpak runs everything through bubblewrap, and bubblewrap calls pivot_root
#   unconditionally. Docker's default seccomp profile has no rule for
#   pivot_root at all, so it returns EPERM — which means --cap-add SYS_ADMIN is
#   *not* enough on its own; you also need --security-opt seccomp=unconfined and
#   (on any AppArmor host, so Debian and Ubuntu) --security-opt
#   apparmor=unconfined. That is --privileged in all but name. Installing the
#   runtime during `docker build` needs more still: BuildKit's
#   `RUN --security=insecure`, which requires a custom buildx builder started
#   with an extra entitlement. Upstream's own flatpak-github-actions simply
#   writes `options: --privileged` and moves on.
#
# So: the host, with FLATPAK_USER_DIR pointed at the lab directory. That is the
# Flatpak equivalent of the fake $HOME — the developer's real --user
# installations are never touched, and nothing needs elevated privileges.
# LAB_FLATPAK_DOCKER=1 switches to the privileged container instead, which is
# what a clean-room check wants and what CI can afford.

: "${LAB_FLATPAK_APP_ID:=org.protonforge.ProtonForge}"

fp_user_dir() { printf '%s' "$LAB_FLATPAK_DIR"; }

# fp_env <command...> -- flatpak against the isolated installation
fp_env() {
    env FLATPAK_USER_DIR="$LAB_FLATPAK_DIR" "$@"
}

fp_require() {
    have flatpak || die "flatpak is missing: sudo apt install flatpak"
    have flatpak-builder || die "flatpak-builder is missing: sudo apt install flatpak-builder"
    python3 -c 'import yaml' 2>/dev/null \
        || die "python3-yaml is missing (needed to derive a buildable manifest): sudo apt install python3-yaml"

    mkdir -p "$LAB_FLATPAK_DIR"
    fp_env flatpak remote-add --user --if-not-exists flathub \
        https://dl.flathub.org/repo/flathub.flatpakrepo >/dev/null 2>&1 \
        || warn "could not add the flathub remote to $LAB_FLATPAK_DIR"
    return 0
}

# fp_runtime_version -> the runtime-version the committed manifest asks for
fp_runtime_version() {
    python3 - "$REPO_ROOT/$LAB_FLATPAK_APP_ID.yml" <<'PY'
import sys, yaml
m = yaml.safe_load(open(sys.argv[1]))
print(m.get("runtime-version", ""))
PY
}

# fp_runtime_available -> 0 when the pinned runtime and SDK can still be had
#
# The manifest pins a version. Flathub retires old branches, so "the runtime the
# manifest asks for no longer exists" is a real state — and a finding in its own
# right rather than a reason for the harness to fall over.
fp_runtime_available() {
    local version; version="$(fp_runtime_version)"
    [[ -n "$version" ]] || return 1
    fp_env flatpak remote-info --user flathub "org.kde.Platform//$version" >/dev/null 2>&1 \
        && fp_env flatpak remote-info --user flathub "org.kde.Sdk//$version" >/dev/null 2>&1
}

fp_install_runtime() {
    local version; version="$(fp_runtime_version)"
    step "Installing org.kde.Platform//$version and org.kde.Sdk//$version (once, ~1.5 GB)"
    fp_env flatpak install --user -y --noninteractive flathub \
        "org.kde.Platform//$version" "org.kde.Sdk//$version" \
        >"${CASE_OUT_DIR:-$LAB_OUT_DIR}/flatpak-runtime.log" 2>&1 \
        || die "could not install the runtime:
$(tail -n 20 "${CASE_OUT_DIR:-$LAB_OUT_DIR}/flatpak-runtime.log")"
}

# fp_devel_manifest -> path of a manifest that builds the working tree
#
# The committed manifest takes its source from a GitHub release tarball with a
# placeholder sha256, so it cannot build a checkout — the release workflow
# rewrites it at tag time. Rather than keep a second copy of the manifest (which
# would drift, and the finish-args are the interesting part), the archive source
# is swapped for a `type: dir` pointing at the repository. Everything else —
# runtime-version, finish-args, post-install — comes from the file that ships.
fp_devel_manifest() {
    local out="$LAB_RUN_DIR/manifest-devel.yml"
    mkdir -p "$LAB_RUN_DIR"

    python3 - "$REPO_ROOT/$LAB_FLATPAK_APP_ID.yml" "$out" "$REPO_ROOT" <<'PY'
import sys, yaml

src, dst, repo = sys.argv[1], sys.argv[2], sys.argv[3]
manifest = yaml.safe_load(open(src))

module = manifest["modules"][0]
kept = [s for s in module.get("sources", []) if s.get("type") != "archive"]

# flatpak-builder resolves a source path relative to the manifest, and this
# manifest is written outside the repository — so the plain filenames the
# committed one uses ("org.protonforge.ProtonForge.desktop", ...) would not be
# found. Absolutise them against the checkout.
import os
for source in kept:
    path = source.get("path")
    if path and not os.path.isabs(path):
        source["path"] = os.path.join(repo, path)

module["sources"] = [{"type": "dir", "path": repo}] + kept

with open(dst, "w") as handle:
    yaml.safe_dump(manifest, handle, sort_keys=False, default_flow_style=False)
PY

    [[ -f "$out" ]] || die "could not derive a devel manifest"
    printf '%s' "$out"
}

# fp_build -- build from the working tree into a local repo
#
# --disable-rofiles-fuse is unconditional, exactly as upstream's own action has
# it. Without it the build fails with a misleading "Build directory not
# initialised, use flatpak build-init" because the rofiles-fuse mount silently
# never comes up. All it costs is the hardlink-checkout cache.
fp_build() {
    local manifest; manifest="$(fp_devel_manifest)"
    local log="${CASE_OUT_DIR:-$LAB_OUT_DIR}/flatpak-build.log"

    step "Building the Flatpak from the working tree"
    ( cd "$LAB_RUN_DIR" && fp_env timeout "$TIMEOUT_FLATPAK" flatpak-builder \
        --user \
        --disable-rofiles-fuse \
        --force-clean \
        --repo="$LAB_RUN_DIR/fp-repo" \
        "$LAB_RUN_DIR/fp-build" \
        "$manifest" ) >"$log" 2>&1
    local rc=$?
    (( rc == 0 )) || {
        err "flatpak-builder failed:
$(tail -n 30 "$log")"
        return 1
    }
    return 0
}

# fp_bundle -> path of the .flatpak bundle
fp_bundle() {
    local out="$LAB_RUN_DIR/protonforge.flatpak"
    fp_env flatpak build-bundle "$LAB_RUN_DIR/fp-repo" "$out" "$LAB_FLATPAK_APP_ID" \
        >"${CASE_OUT_DIR:-$LAB_OUT_DIR}/flatpak-bundle.log" 2>&1 || return 1
    printf '%s' "$out"
}

fp_install_bundle() {
    local bundle="$1"
    fp_env flatpak install --user -y --noninteractive "$bundle" \
        >"${CASE_OUT_DIR:-$LAB_OUT_DIR}/flatpak-install.log" 2>&1
}

fp_installed() {
    fp_env flatpak info "$LAB_FLATPAK_APP_ID" >/dev/null 2>&1
}

# fp_run <args...> -- run the installed app
#
# The Steam fixture trees live under $LAB_APP_HOME, and the sandbox only reaches
# what finish-args let it — which is exactly what 70_flatpak is checking, so
# HOME is handed over as-is rather than worked around.
fp_run() {
    fp_env timeout "$TIMEOUT_CLI" flatpak run \
        --env=HOME="$LAB_APP_HOME" \
        --env=XDG_CONFIG_HOME="$LAB_APP_HOME/.config" \
        --env=XDG_CACHE_HOME="$LAB_APP_HOME/.cache" \
        --env=PROTONFORGE_NO_STARTUP_CHECKS=1 \
        --filesystem="$LAB_APP_HOME" \
        "$LAB_FLATPAK_APP_ID" "$@"
}

# fp_run_bare <args...> -- no extra --filesystem, so only the manifest's own
# finish-args decide what is visible. This is what proves they are sufficient.
fp_run_bare() {
    fp_env timeout "$TIMEOUT_CLI" flatpak run \
        --env=PROTONFORGE_NO_STARTUP_CHECKS=1 \
        "$LAB_FLATPAK_APP_ID" "$@"
}

# fp_ls <path inside /app> -> a listing, for the layout assertions
fp_ls() {
    fp_env flatpak run --command=ls "$LAB_FLATPAK_APP_ID" -la "$1" 2>/dev/null
}

# fp_test_file <path inside the sandbox> -> 0 when it is readable there
#
# -e rather than -r so it works for directories as well as files; readability of a
# directory means something different and is not what any caller is asking.
fp_test_file() {
    fp_env flatpak run --command=test "$LAB_FLATPAK_APP_ID" -e "$1" 2>/dev/null
}

fp_cleanup() {
    fp_installed && fp_env flatpak uninstall --user -y "$LAB_FLATPAK_APP_ID" >/dev/null 2>&1
    return 0
}
