#!/usr/bin/env bash
# changelog-section.sh <version> [changelog] -> the body of that version's section
#
# Prints everything under "## [<version>]" in CHANGELOG.md up to the next
# "## [" heading, without the heading itself. Exits 1 if the section is
# missing or empty — the release workflow uses that to refuse a version
# nobody wrote notes for, and to put the notes into the GitHub release.
set -euo pipefail

version="${1:?usage: changelog-section.sh <version> [changelog]}"
file="${2:-$(dirname "$0")/../CHANGELOG.md}"

section="$(awk -v v="$version" '
    index($0, "## [" v "]") == 1 { found = 1; next }
    found && /^## \[/            { exit }
    found                        { print }
' "$file")"

# Trim leading and trailing blank lines; an all-blank section is no section.
section="$(printf '%s\n' "$section" | sed -e '/./,$!d' | sed -e ':a' -e '/^\n*$/{$d;N;ba' -e '}')"
if [[ -z "${section//[[:space:]]/}" ]]; then
    echo "CHANGELOG.md has no notes for ${version}" >&2
    exit 1
fi
printf '%s\n' "$section"
