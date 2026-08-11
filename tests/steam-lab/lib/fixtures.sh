# shellcheck shell=bash
#
# Synthetic Steam installations.
#
# This is the lab's equivalent of a fixture factory: it writes exactly the files
# ProtonForge reads, and nothing else. Everything it produces is hand-authored
# in Steam's own formats, so the app's real parsers do the real work — there are
# no mocks anywhere in this suite.
#
# The one gate that decides whether any of it is visible:
# SteamPaths::hasLibraryFolders() (SteamPaths.cpp:21) requires
# <root>/steamapps/libraryfolders.vdf to exist as a regular file. Without it the
# variant is None, SteamLauncher::isAvailable() is false, and LauncherManager
# never even registers the launcher. That is why fx_steam_tree always writes it
# and why the "bootstrap" variant — which deliberately does not — is a useful
# case of its own.
#
# Every function prints the path it wrote, and rejects an unknown key rather
# than silently ignoring a typo in a case.

# The layouts, keyed by variant. These are the paths SteamPaths probes for
# (SteamPaths.cpp:54) — keep them in step with that list.
fx_native_root()  { printf '%s' "$LAB_APP_HOME/.local/share/Steam"; }
fx_flatpak_root() { printf '%s' "$LAB_APP_HOME/.var/app/com.valvesoftware.Steam/.local/share/Steam"; }

# fx_reset -- start from nothing
fx_reset() {
    app_home_reset
}

# _fx_write_libraryfolders <root> [extra library roots...]
_fx_write_libraryfolders() {
    local root="$1"; shift
    local file="$root/steamapps/libraryfolders.vdf"
    mkdir -p "$root/steamapps"

    {
        printf '"libraryfolders"\n{\n'
        printf '\t"0"\n\t{\n'
        printf '\t\t"path"\t\t"%s"\n' "$root"
        printf '\t\t"label"\t\t""\n'
        printf '\t\t"contentid"\t\t"6386869439825518753"\n'
        printf '\t\t"totalsize"\t\t"0"\n'
        printf '\t\t"apps"\n\t\t{\n\t\t}\n'
        printf '\t}\n'
        local i=1 extra
        for extra in "$@"; do
            mkdir -p "$extra/steamapps/common"
            printf '\t"%d"\n\t{\n' "$i"
            printf '\t\t"path"\t\t"%s"\n' "$extra"
            printf '\t\t"label"\t\t"extra%d"\n' "$i"
            printf '\t\t"apps"\n\t\t{\n\t\t}\n'
            printf '\t}\n'
            i=$(( i + 1 ))
        done
        printf '}\n'
    } >"$file"

    printf '%s' "$file"
}

# fx_steam_tree <native|flatpak|both|none|bootstrap> [key=value ...] -> root path
#
#   native     ~/.local/share/Steam plus the symlink farm a real install has
#   flatpak    ~/.var/app/com.valvesoftware.Steam/.local/share/Steam
#   both       both of the above, to exercise the native-wins tie-break
#   none       nothing at all
#   bootstrap  what Valve's bootstrap tarball leaves behind and no more: the
#              directory and the ~/.steam/steam symlink, but no steamapps and
#              no libraryfolders.vdf. ProtonForge cannot see this installation.
#
# Keys:
#   libraries    space-separated extra library roots
#   compat_tool  name of a fake Proton in compatibilitytools.d ("" for none)
#   overlay      yes|no  — the gameoverlayrenderer.so pair
#   runtime      yes|no  — ubuntu12_32/steam-runtime
fx_steam_tree() {
    local variant="$1"; shift

    declare -A p=(
        [libraries]=""
        [compat_tool]="proton-cachyos-11.0-20260703-slr-x86_64"
        [overlay]="yes"
        [runtime]="yes"
    )

    local kv key
    for kv in "$@"; do
        key="${kv%%=*}"
        [[ -v p["$key"] ]] || die "fx_steam_tree: unknown key '$key'"
        p["$key"]="${kv#*=}"
    done

    app_home_init

    case "$variant" in
    none)
        printf ''
        return 0
        ;;
    bootstrap)
        local root; root="$(fx_native_root)"
        mkdir -p "$root/ubuntu12_32/steam-runtime" "$root/ubuntu12_64" "$root/clientui"
        : >"$root/steam.sh"
        chmod +x "$root/steam.sh"
        mkdir -p "$LAB_APP_HOME/.steam"
        ln -sfn "$root" "$LAB_APP_HOME/.steam/steam"
        printf '%s' "$root"
        return 0
        ;;
    native|flatpak|both) ;;
    *) die "fx_steam_tree: unknown variant '$variant' (native|flatpak|both|none|bootstrap)" ;;
    esac

    local roots=()
    [[ "$variant" == "native" || "$variant" == "both" ]] && roots+=("$(fx_native_root)")
    [[ "$variant" == "flatpak" || "$variant" == "both" ]] && roots+=("$(fx_flatpak_root)")

    local root
    for root in "${roots[@]}"; do
        mkdir -p "$root/steamapps/common" \
                 "$root/steamapps/compatdata" \
                 "$root/steamapps/shadercache" \
                 "$root/compatibilitytools.d" \
                 "$root/config" \
                 "$root/userdata"

        # shellcheck disable=SC2086
        _fx_write_libraryfolders "$root" ${p[libraries]} >/dev/null

        if [[ "${p[overlay]}" == "yes" ]]; then
            mkdir -p "$root/ubuntu12_64" "$root/ubuntu12_32"
            : >"$root/ubuntu12_64/gameoverlayrenderer.so"
            : >"$root/ubuntu12_32/gameoverlayrenderer.so"
        fi
        if [[ "${p[runtime]}" == "yes" ]]; then
            mkdir -p "$root/ubuntu12_32/steam-runtime"
        fi
        if [[ -n "${p[compat_tool]}" ]]; then
            fx_compat_tool "$root" "${p[compat_tool]}" >/dev/null
        fi
    done

    # The symlink farm a real native install has. ~/.steam/root and
    # ~/.steam/steam are what SteamPaths canonicalises through, so a fixture
    # without them would not exercise that path at all.
    if [[ "$variant" == "native" || "$variant" == "both" ]]; then
        mkdir -p "$LAB_APP_HOME/.steam"
        ln -sfn "$(fx_native_root)" "$LAB_APP_HOME/.steam/steam"
        ln -sfn "$(fx_native_root)" "$LAB_APP_HOME/.steam/root"
        ln -sfn "$(fx_native_root)/ubuntu12_32" "$LAB_APP_HOME/.steam/bin32"
        ln -sfn "$(fx_native_root)/ubuntu12_64" "$LAB_APP_HOME/.steam/bin64"
    fi
    if [[ "$variant" == "flatpak" ]]; then
        mkdir -p "$LAB_APP_HOME/.var/app/com.valvesoftware.Steam/.steam"
    fi

    printf '%s' "${roots[0]}"
}

# fx_add_game <root> <appid> [key=value ...] -> path of the appmanifest
#
# Keys:
#   name         display name
#   installdir   directory under steamapps/common
#   stateflags   4 = fully installed, 6 = update pending (Game::needsUpdate)
#   buildid      -
#   sizeondisk   -
#   exe          windows|native|none — decides SteamLauncher's isNativeLinux
#                scan (SteamLauncher.cpp:164) and what GameRunner can find
#   library      write into this library root instead of <root>
fx_add_game() {
    local root="$1" appid="$2"; shift 2

    declare -A p=(
        [name]="Test Game $appid"
        [installdir]=""
        [stateflags]="4"
        [buildid]="12345678"
        [sizeondisk]="1073741824"
        [exe]="windows"
        [library]=""
    )

    local kv key
    for kv in "$@"; do
        key="${kv%%=*}"
        [[ -v p["$key"] ]] || die "fx_add_game: unknown key '$key'"
        p["$key"]="${kv#*=}"
    done

    [[ -n "${p[installdir]}" ]] || p[installdir]="${p[name]}"

    local library="${p[library]:-$root}"
    local steamapps="$library/steamapps"
    [[ -d "$steamapps" ]] || steamapps="$library"     # extra libraries are given as their root
    mkdir -p "$steamapps/common/${p[installdir]}"

    local installPath="$steamapps/common/${p[installdir]}"
    case "${p[exe]}" in
    windows)
        # One .exe anywhere under the install dir is enough to mark the game
        # non-native; the name is matched against the game name to pick a main
        # executable, so give it the obvious one.
        printf 'not a real pe binary\n' >"$installPath/${p[installdir]}.exe"
        ;;
    native)
        printf '#!/bin/sh\necho native game\n' >"$installPath/${p[installdir]}"
        chmod +x "$installPath/${p[installdir]}"
        ;;
    none) ;;
    *) die "fx_add_game: exe must be windows|native|none, got '${p[exe]}'" ;;
    esac

    local manifest="$steamapps/appmanifest_${appid}.acf"
    cat >"$manifest" <<EOF
"AppState"
{
	"appid"		"${appid}"
	"Universe"		"1"
	"name"		"${p[name]}"
	"StateFlags"		"${p[stateflags]}"
	"installdir"		"${p[installdir]}"
	"lastupdated"		"1785491471"
	"LastPlayed"		"0"
	"SizeOnDisk"		"${p[sizeondisk]}"
	"StagingSize"		"0"
	"buildid"		"${p[buildid]}"
	"LastOwner"		"76561197981283578"
	"UpdateResult"		"0"
	"BytesToDownload"		"1578992"
	"BytesDownloaded"		"0"
	"TargetBuildID"		"${p[buildid]}"
	"AutoUpdateBehavior"		"0"
	"InstalledDepots"
	{
		"$(( appid + 1 ))"
		{
			"manifest"		"1234567890123456789"
			"size"		"${p[sizeondisk]}"
		}
	}
}
EOF

    printf '%s' "$manifest"
}

# fx_compat_tool <root> <name> [require_tool_appid] -> the tool directory
#
# A Proton is any directory holding an executable file called `proton`
# (ProtonManager.cpp:41, GameRunner.cpp:34). With require_tool_appid set, its
# toolmanifest.vdf asks for a Steam Linux Runtime and GameRunner switches to the
# container chain (GameRunner.cpp:210).
fx_compat_tool() {
    local root="$1" name="$2" requireToolAppId="${3:-}"
    local dir="$root/compatibilitytools.d/$name"
    mkdir -p "$dir"

    printf '#!/bin/sh\nexit 0\n' >"$dir/proton"
    chmod +x "$dir/proton"

    if [[ -n "$requireToolAppId" ]]; then
        cat >"$dir/toolmanifest.vdf" <<EOF
"manifest"
{
	"version"		"2"
	"commandline"		"/proton waitforexitandrun"
	"require_tool_appid"		"${requireToolAppId}"
}
EOF
    else
        cat >"$dir/toolmanifest.vdf" <<'EOF'
"manifest"
{
	"version"		"2"
	"commandline"		"/proton run"
}
EOF
    fi

    printf '%s' "$dir"
}

# fx_localconfig <root> [key=value ...] <appid>=<launchoptions> ... -> the file
#
# Keys:
#   userid   the userdata subdirectory
#   casing   canonical | lower — Steam's own key casing varies between client
#            versions, which is why SteamLauncher::readLaunchOptions walks the
#            tree case-insensitively (SteamLauncher.cpp:267,281). Both have to
#            work.
#   nested   no | after | before — whether the app section contains a nested {}
#            block, and on which side of LaunchOptions.
#
#            This matters because SteamLauncher::writeToLocalConfig finds the
#            section with "\"<appid>\"\s*\{[^}]*\}" (SteamLauncher.cpp:229), and
#            a [^}]* body stops at the *first* closing brace. With the nested
#            block after LaunchOptions the match is short but still contains
#            LaunchOptions, so the replacement happens to work. With it before,
#            LaunchOptions falls outside the match and the insertion lands inside
#            the nested block instead. Real localconfig.vdf sections contain
#            several such blocks, in no guaranteed order.
fx_localconfig() {
    local root="$1"; shift

    declare -A p=(
        [userid]="21017850"
        [casing]="canonical"
        [nested]="no"
    )

    local -a apps=()
    local kv key
    for kv in "$@"; do
        key="${kv%%=*}"
        if [[ -v p["$key"] ]]; then
            p["$key"]="${kv#*=}"
        elif [[ "$key" =~ ^[0-9]+$ ]]; then
            apps+=("$kv")
        else
            die "fx_localconfig: unknown key '$key'"
        fi
    done

    local kStore kSoftware kValve kSteam kApps
    if [[ "${p[casing]}" == "lower" ]]; then
        kStore="userlocalconfigstore"; kSoftware="software"; kValve="valve"
        kSteam="steam"; kApps="apps"
    else
        kStore="UserLocalConfigStore"; kSoftware="Software"; kValve="Valve"
        kSteam="Steam"; kApps="Apps"
    fi

    local dir="$root/userdata/${p[userid]}/config"
    mkdir -p "$dir"
    local file="$dir/localconfig.vdf"

    {
        printf '"%s"\n{\n' "$kStore"
        printf '\t"%s"\n\t{\n' "$kSoftware"
        printf '\t\t"%s"\n\t\t{\n' "$kValve"
        printf '\t\t\t"%s"\n\t\t\t{\n' "$kSteam"
        printf '\t\t\t\t"%s"\n\t\t\t\t{\n' "$kApps"
        local entry appid opts
        for entry in "${apps[@]}"; do
            appid="${entry%%=*}"
            opts="${entry#*=}"
            printf '\t\t\t\t\t"%s"\n\t\t\t\t\t{\n' "$appid"
            printf '\t\t\t\t\t\t"LastPlayed"\t\t"1700000000"\n'
            # Real localconfig.vdf app sections routinely contain nested blocks.
            if [[ "${p[nested]}" == "before" ]]; then
                printf '\t\t\t\t\t\t"BadgeData"\n\t\t\t\t\t\t{\n'
                printf '\t\t\t\t\t\t\t"level"\t\t"5"\n'
                printf '\t\t\t\t\t\t}\n'
            fi
            # An empty value writes the key with an empty string, which is what
            # Steam does for a game whose launch options were cleared. Pass the
            # literal word "absent" to leave the key out entirely.
            if [[ "$opts" != "absent" ]]; then
                printf '\t\t\t\t\t\t"LaunchOptions"\t\t"%s"\n' "${opts//\"/\\\"}"
            fi
            if [[ "${p[nested]}" == "yes" || "${p[nested]}" == "after" ]]; then
                printf '\t\t\t\t\t\t"BadgeData"\n\t\t\t\t\t\t{\n'
                printf '\t\t\t\t\t\t\t"level"\t\t"5"\n'
                printf '\t\t\t\t\t\t}\n'
            fi
            printf '\t\t\t\t\t}\n'
        done
        printf '\t\t\t\t}\n'
        printf '\t\t\t}\n\t\t}\n\t}\n}\n'
    } >"$file"

    printf '%s' "$file"
}

# fx_config_vdf <root> [<appid>=<toolname> ...] -> the file
#
# Per-game compat tool mapping, which GameRunner::findProtonFromConfig reads
# and which outranks the compatibilitytools.d scan (GameRunner.cpp:288).
fx_config_vdf() {
    local root="$1"; shift
    local dir="$root/config"
    mkdir -p "$dir"
    local file="$dir/config.vdf"

    {
        printf '"InstallConfigStore"\n{\n'
        printf '\t"Software"\n\t{\n'
        printf '\t\t"Valve"\n\t\t{\n'
        printf '\t\t\t"Steam"\n\t\t\t{\n'
        printf '\t\t\t\t"CompatToolMapping"\n\t\t\t\t{\n'
        local entry appid tool
        for entry in "$@"; do
            appid="${entry%%=*}"
            tool="${entry#*=}"
            printf '\t\t\t\t\t"%s"\n\t\t\t\t\t{\n' "$appid"
            printf '\t\t\t\t\t\t"name"\t\t"%s"\n' "$tool"
            printf '\t\t\t\t\t\t"config"\t\t""\n'
            printf '\t\t\t\t\t\t"priority"\t\t"250"\n'
            printf '\t\t\t\t\t}\n'
        done
        printf '\t\t\t\t}\n'
        printf '\t\t\t}\n\t\t}\n\t}\n}\n'
    } >"$file"

    printf '%s' "$file"
}

# fx_settings <gamekey>=<json> ... -> the settings file
#
# Writes ~/.config/ProtonForge/settings.json directly, so a case can start from
# a known per-game configuration without going through the GUI. The game key is
# Game::settingsKey(), i.e. "Steam:<appid>". The two top-level keys are
# "defaults" and "games" — see SettingsManager::save().
fx_settings() {
    local dir="$LAB_APP_HOME/.config/ProtonForge"
    mkdir -p "$dir"
    local file="$dir/settings.json"

    python3 - "$file" "$@" <<'PY'
import json, sys
path, pairs = sys.argv[1], sys.argv[2:]
games, defaults = {}, {}
for pair in pairs:
    key, _, blob = pair.partition("=")
    if key == "defaults":
        defaults = json.loads(blob)
    else:
        games[key] = json.loads(blob)
json.dump({"defaults": defaults, "games": games}, open(path, "w"), indent=2)
PY

    printf '%s' "$file"
}

# fx_settings_get <gamekey> <field> -> the stored value, or nothing
#
# For assertions on what the app wrote back to disk.
fx_settings_get() {
    local file="$LAB_APP_HOME/.config/ProtonForge/settings.json"
    [[ -f "$file" ]] || return 1
    python3 - "$file" "$1" "$2" <<'PY'
import json, sys
path, key, field = sys.argv[1], sys.argv[2], sys.argv[3]
try:
    doc = json.load(open(path))
except Exception:
    sys.exit(1)
section = doc.get("defaults", {}) if key == "defaults" else doc.get("games", {}).get(key, {})
value = section.get(field)
if isinstance(value, bool):
    print("true" if value else "false")
elif value is None:
    sys.exit(1)
else:
    print(value)
PY
}

# --- GOG ---------------------------------------------------------------------
#
# GOG has no client and no Steam-shaped tree to fabricate: what ProtonForge
# reads is its own install registry plus whatever the downloader left on disk.
# So these write exactly those two things, and nothing else.
#
# The registry is the single source of truth (GogInstallRegistry) — there is no
# filesystem scan to fool, which is why a case that wants a GOG game visible
# must call fx_gog_game rather than just creating directories.

# fx_gog_root -- where installs live, matching GogInstallRegistry::installRoot()
fx_gog_root() { printf '%s' "$LAB_APP_HOME/Games/ProtonForge"; }

# fx_gog_tree -- an empty but configured GOG install root
#
# Prints the root. Nothing is discoverable until fx_gog_game adds an entry.
fx_gog_tree() {
    local root; root="$(fx_gog_root)"
    mkdir -p "$root/GOG" "$root/prefixes/GOG"
    printf '%s' "$root"
}

# fx_gog_game <productid> [key=value ...] -> the install directory
#
#   title=       display name, default "GOG Game <productid>"
#   platform=    windows|linux, default windows
#   exe=         windows|native|none, default windows
#   build=       installed build id, default 1000
#   latest=      newest known build id, default same as build (so no update)
#   complete=    yes|no, default yes
#   args=        launch arguments, space separated
#   info=        yes|no — also drop a goggame-<id>.info file, default no
#   image=       recorded banner URL, default none (the "not looked up yet"
#                state, which is what an install made before artwork was
#                recorded looks like)
#
# Writes the game tree and appends to gog-installs.json, which is what
# GogLauncher::discoverGames actually reads.
fx_gog_game() {
    local productId="$1"; shift
    [[ -n "$productId" ]] || die "fx_gog_game: needs a product id"

    declare -A p=(
        [title]="GOG Game $productId" [platform]=windows [exe]=windows
        [build]=1000 [latest]= [complete]=yes [args]= [info]=no [image]=
    )
    local arg key value
    for arg in "$@"; do
        key="${arg%%=*}"; value="${arg#*=}"
        [[ -v p["$key"] ]] || die "fx_gog_game: unknown key '$key'"
        p["$key"]="$value"
    done
    [[ -n "${p[latest]}" ]] || p[latest]="${p[build]}"

    local root; root="$(fx_gog_tree)"
    local dir="$root/GOG/${p[title]}"
    mkdir -p "$dir"

    local exePath="" workDir=""
    case "${p[exe]}" in
        windows)
            mkdir -p "$dir/bin"
            printf 'stub\n' >"$dir/bin/game.exe"
            exePath="$dir/bin/game.exe"; workDir="$dir/bin" ;;
        native)
            printf '#!/bin/sh\necho native game\n' >"$dir/start.sh"
            chmod +x "$dir/start.sh"
            exePath="$dir/start.sh"; workDir="$dir" ;;
        none) ;;
        *) die "fx_gog_game: exe must be windows|native|none, got '${p[exe]}'" ;;
    esac

    if [[ "${p[info]}" == yes ]]; then
        cp "$LAB_SRC_DIR/fixtures/gog/goggame-windows.info" \
           "$dir/goggame-${productId}.info" 2>/dev/null || true
    fi

    python3 - "$LAB_APP_HOME/.config/ProtonForge/gog-installs.json" \
        "$productId" "${p[title]}" "$dir" "${p[platform]}" "${p[build]}" "${p[latest]}" \
        "${p[complete]}" "$exePath" "$workDir" "${p[args]}" "${p[image]}" <<'PY'
import json, os, sys
(path, pid, title, install, platform, build, latest,
 complete, exe, workdir, args, image) = sys.argv[1:13]

os.makedirs(os.path.dirname(path), exist_ok=True)
try:
    doc = json.load(open(path))
except Exception:
    doc = {"version": 1, "installs": []}

doc.setdefault("installs", [])
doc["installs"] = [e for e in doc["installs"] if e.get("productId") != pid]
doc["installs"].append({
    "productId": pid,
    "title": title,
    "installPath": install,
    "platform": platform,
    "buildId": build,
    "latestBuildId": latest,
    "complete": complete == "yes",
    "nativeLinux": platform == "linux",
    "executablePath": exe,
    "workingDirectory": workdir,
    "launchArgs": args.split() if args else [],
    "languages": ["en-US"],
    "size": 1024,
    **({"imageUrl": image} if image else {}),
})
json.dump(doc, open(path, "w"), indent=2)
PY

    printf '%s' "$dir"
}

# fx_gog_signin -- pretend a GOG session was restored
#
# Only the refresh token is persisted, and SecretStore's file backend is what
# the lab uses (PROTONFORGE_SECRET_STORE=file). The token is deliberately
# invalid: no case may reach GOG, and one that tries must fail visibly rather
# than quietly succeed against somebody's real account.
fx_gog_signin() {
    local dir="$LAB_APP_HOME/.config/ProtonForge"
    mkdir -p "$dir"
    printf '{"gog-refresh-token": "lab-fixture-not-a-real-token"}\n' >"$dir/secrets.json"
    chmod 600 "$dir/secrets.json"
    printf '%s' "$dir/secrets.json"
}

# fx_gog_signout -- no stored credentials at all
fx_gog_signout() {
    rm -f "$LAB_APP_HOME/.config/ProtonForge/secrets.json"
}
