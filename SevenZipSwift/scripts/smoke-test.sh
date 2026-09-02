#!/bin/bash
# ══════════════════════════════════════════════════════════════════════
# End-to-end smoke test for the 7z CLI.
#
#   ./scripts/smoke-test.sh [path/to/7z]
#
# With no argument the release CLI is built first. Every case exercises the
# real archive engine — there are no mocks — so a pass means create, list and
# extract genuinely round-trip on this machine.
# ══════════════════════════════════════════════════════════════════════
set -uo pipefail

# Nothing here is interactive; make sure a stray prompt can never block a run.
exec </dev/null

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
GREEN='\033[0;32m'; RED='\033[0;31m'; BOLD='\033[1m'; NC='\033[0m'

pass=0
fail=0

ok()   { printf "  ${GREEN}✓${NC} %s\n" "$1"; pass=$((pass + 1)); }
bad()  { printf "  ${RED}✗${NC} %s\n" "$1"; [ $# -gt 1 ] && printf "      %s\n" "$2"; fail=$((fail + 1)); }
section() { printf "\n${BOLD}%s${NC}\n" "$1"; }

# ── Locate the binary ────────────────────────────────────────────────
if [ $# -ge 1 ]; then
    BIN="$1"
else
    echo "Building 7z (release)..."
    (cd "$PROJECT_DIR" && swift build --product 7z -c release) || { echo "build failed"; exit 1; }
    BIN="$(find "$PROJECT_DIR/.build" -maxdepth 3 -type f -name 7z -perm -111 2>/dev/null | head -1)"
fi
[ -x "$BIN" ] || { echo "7z binary not found: ${BIN:-<none>}"; exit 1; }
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
echo "Testing: $BIN"
"$BIN" --tool >/dev/null 2>&1 || { echo "No archive engine available; install p7zip."; exit 1; }
echo "Engine:  $("$BIN" --tool)"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
cd "$WORK" || exit 1

# ── Fixtures ─────────────────────────────────────────────────────────
mkdir -p src/sub
echo "hello world" > src/a.txt
echo "second file" > src/b.txt
echo "nested"      > src/sub/c.txt
# Names that broke the column parser: consecutive spaces and a very long name.
echo "spaced"      > "src/two  spaces.txt"
echo "long"        > "src/$(printf 'l%.0s' $(seq 1 90)).txt"

TEMP_BEFORE="$(ls -d "${TMPDIR:-/tmp}"/7z-* 2>/dev/null | wc -l | tr -d ' ')"

# ── Round-trip every creatable format ────────────────────────────────
section "Round-trip (create → list → extract → compare)"
for fmt in 7z zip tar tar.gz tar.bz2 tar.xz; do
    archive="rt.$fmt"
    out="out_$fmt"
    rm -rf "$archive" "$out"

    if ! "$BIN" create "$archive" src >/dev/null 2>&1; then
        bad "$fmt: create failed"; continue
    fi
    if [ ! -s "$archive" ]; then
        bad "$fmt: archive is empty"; continue
    fi
    if ! "$BIN" extract "$archive" "$out" >/dev/null 2>&1; then
        bad "$fmt: extract failed"; continue
    fi
    # Compound formats yield an intermediate tar; unwrap it.
    if [ -f "$out/rt.tar" ]; then
        (cd "$out" && tar xf rt.tar && rm -f rt.tar)
    fi
    if diff -r src "$out/src" >/dev/null 2>&1; then
        ok "$fmt round-trips byte-for-byte"
    else
        bad "$fmt: extracted tree differs from source"
    fi
done

# ── Format is inferred from the destination extension ────────────────
section "Format inference (no -t flag)"
check_type() {  # <file> <pattern> <label>
    if file "$1" | grep -qi "$2"; then ok "$3"; else bad "$3" "$(file "$1")"; fi
}
"$BIN" create inf.7z      src >/dev/null 2>&1; check_type inf.7z      "7-zip"  ".7z  → 7z container"
"$BIN" create inf.zip     src >/dev/null 2>&1; check_type inf.zip     "zip"    ".zip → zip container"
"$BIN" create inf.tar.gz  src >/dev/null 2>&1; check_type inf.tar.gz  "gzip"   ".tar.gz → gzip container"
"$BIN" create inf.tar.bz2 src >/dev/null 2>&1; check_type inf.tar.bz2 "bzip2"  ".tar.bz2 → bzip2 container"
"$BIN" create inf.tar.xz  src >/dev/null 2>&1; check_type inf.tar.xz  "XZ"     ".tar.xz → xz container"

# An explicit -t still wins over the extension.
"$BIN" create -t zip explicit.7z src >/dev/null 2>&1
check_type explicit.7z "zip" "-t zip overrides the .7z extension"

# ── Compound archives name their inner tar after the archive ─────────
section "Compound archive internals"
if gzip -lv inf.tar.gz 2>/dev/null | tail -1 | grep -q "inf.tar"; then
    ok "gzip stores the inner tar as 'inf.tar'"
else
    bad "gzip inner name is wrong" "$(gzip -lv inf.tar.gz 2>/dev/null | tail -1)"
fi
if gzip -t inf.tar.gz 2>/dev/null && tar tzf inf.tar.gz >/dev/null 2>&1; then
    ok "tar.gz passes a gzip integrity check and lists as tar"
else
    bad "tar.gz is not a valid gzip/tar stream"
fi
# Re-creating over an existing archive must not fail.
if "$BIN" create inf.tar.gz src >/dev/null 2>&1 && tar tzf inf.tar.gz >/dev/null 2>&1; then
    ok "re-creating over an existing tar.gz succeeds"
else
    bad "re-creating over an existing tar.gz failed"
fi

# The compression stage writes beside the destination and renames on success,
# so a failed rebuild must leave the previous archive untouched. (Skipped for
# root, which can write to a read-only directory anyway.)
if [ "$(id -u)" -ne 0 ]; then
    mkdir -p atomic/d && echo data > atomic/d/f.txt
    ( cd atomic && "$BIN" create keep.tar.gz d >/dev/null 2>&1 )
    before="$(shasum atomic/keep.tar.gz 2>/dev/null | cut -d' ' -f1)"
    chmod 555 atomic
    ( cd atomic && "$BIN" create keep.tar.gz d >/dev/null 2>&1 )
    rebuild_rc=$?
    chmod 755 atomic
    after="$(shasum atomic/keep.tar.gz 2>/dev/null | cut -d' ' -f1)"
    leftovers="$(ls atomic/*.7z-part* 2>/dev/null | wc -l | tr -d ' ')"
    if [ "$rebuild_rc" -eq 0 ]; then
        bad "a failed rebuild reported success"
    elif [ -n "$before" ] && [ "$before" = "$after" ] && [ "$leftovers" -eq 0 ]; then
        ok "a failed rebuild leaves the previous archive intact"
    else
        bad "a failed rebuild damaged the existing archive or left a partial file"
    fi
fi

# ── Listing preserves awkward names ──────────────────────────────────
section "Listing"
listing="$("$BIN" list rt.tar 2>/dev/null)"
if echo "$listing" | grep -q "two  spaces.txt"; then
    ok "names with consecutive spaces survive parsing"
else
    bad "consecutive spaces in a filename were mangled"
fi
if echo "$listing" | grep -q "$(printf 'l%.0s' $(seq 1 90))"; then
    ok "long names are not truncated"
else
    bad "a 90-character name was truncated"
fi
if echo "$listing" | grep -q "\[DIR\] src/sub"; then
    ok "directories are flagged"
else
    bad "directory entries are not flagged"
fi
# Solid 7z archives leave the packed column blank for most entries.
if [ "$("$BIN" list rt.7z 2>/dev/null | grep -c 'src/')" -ge 4 ]; then
    ok "solid 7z listing keeps every entry"
else
    bad "entries lost when the packed column is blank"
fi

# A blank packed column combined with a two-space filename used to abort the
# process with an uncaught std::invalid_argument from stoll.
mkdir -p solid && echo one > "solid/two  spaces.txt" && echo two > solid/other.txt
"$BIN" create -t 7z solid.7z solid >/dev/null 2>&1
"$BIN" list solid.7z >/dev/null 2>&1
rc=$?
if [ "$rc" -ge 128 ]; then
    bad "listing crashed (signal $((rc - 128))) on a blank packed column"
elif [ "$rc" -ne 0 ]; then
    bad "listing failed (exit $rc) on a blank packed column"
elif "$BIN" list solid.7z 2>/dev/null | grep -q "two  spaces.txt"; then
    ok "blank packed column with a spaced name parses without crashing"
else
    bad "spaced name lost when the packed column is blank"
fi

# ── Passwords ────────────────────────────────────────────────────────
section "Encryption"
"$BIN" create -t 7z -p hunter2 enc.7z src >/dev/null 2>&1
if "$BIN" list enc.7z >/dev/null 2>&1; then
    bad "encrypted archive listed without a password"
else
    ok "encrypted archive refuses to list without a password"
fi
if "$BIN" list -p hunter2 enc.7z >/dev/null 2>&1; then
    ok "correct password lists the archive"
else
    bad "correct password was rejected"
fi
rm -rf encout
if "$BIN" extract -p hunter2 enc.7z encout >/dev/null 2>&1 && diff -r src encout/src >/dev/null 2>&1; then
    ok "encrypted archive round-trips with the password"
else
    bad "encrypted extraction did not match the source"
fi

# ── Error handling ───────────────────────────────────────────────────
section "Error handling"
expect_fail() {  # <label> <args...>
    local label="$1"; shift
    if "$BIN" "$@" >/dev/null 2>&1; then bad "$label (expected non-zero exit)"; else ok "$label"; fi
}
expect_fail "missing archive exits non-zero"       list  no-such-file.zip
expect_fail "unsupported extension is rejected"    list  src/a.txt
expect_fail "unknown option is rejected"           --bogus
expect_fail "creating with no inputs is rejected"  create empty.zip
expect_fail "creating from a missing file fails"   create x.zip no-such-file.txt

# ── Housekeeping ─────────────────────────────────────────────────────
section "Housekeeping"
TEMP_AFTER="$(ls -d "${TMPDIR:-/tmp}"/7z-* 2>/dev/null | wc -l | tr -d ' ')"
if [ "$TEMP_AFTER" -le "$TEMP_BEFORE" ]; then
    ok "no temporary files left behind"
else
    bad "leaked $((TEMP_AFTER - TEMP_BEFORE)) temp file(s) in ${TMPDIR:-/tmp}"
fi

# ── Summary ──────────────────────────────────────────────────────────
printf "\n${BOLD}%d passed, %d failed${NC}\n" "$pass" "$fail"
[ "$fail" -eq 0 ] || exit 1
