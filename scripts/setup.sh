#!/usr/bin/env sh
# ---------------------------------------------------------------------------
#  Arbeitsbaum einrichten: AskSin++-Submodul holen und Patches anwenden.
#
#  Warum ueberhaupt ein Skript? AskSin++ ist Fremdcode unter CC BY-NC-SA und
#  liegt deshalb nicht in diesem Repository, sondern als Submodul. Die eine
#  noetige Aenderung daran (siehe NOTICE.md) ist ein Patch, der erst hier auf
#  den Arbeitsbaum angewandt wird -- upstream/ wird nie direkt editiert
#  (CLAUDE.md §2.2.1).
#
#  Mehrfach aufrufbar: bereits angewandte Patches werden erkannt und
#  uebersprungen.
#
#  Aufruf:
#      ./scripts/setup.sh                 Submodul holen + Patches anwenden
#      ./scripts/setup.sh --no-submodule  nur Patches (CI holt das Submodul
#                                         bereits ueber actions/checkout)
# ---------------------------------------------------------------------------
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
upstream="$root/lib/asksinpp/upstream"
patchdir="$root/lib/asksinpp/patches"

with_submodule=1
case "${1:-}" in
    --no-submodule) with_submodule=0 ;;
    "") ;;
    *) echo "Unbekanntes Argument: $1" >&2; exit 2 ;;
esac

if ! command -v git >/dev/null 2>&1; then
    echo "FEHLER: git nicht gefunden. Es wird fuer das Submodul gebraucht." >&2
    exit 1
fi

if [ "$with_submodule" -eq 1 ]; then
    echo "Submodul lib/asksinpp/upstream holen ..."
    git -C "$root" submodule update --init --recursive
fi

if [ ! -f "$upstream/AskSinPP.h" ]; then
    echo "FEHLER: $upstream ist leer." >&2
    echo "        Das Submodul fehlt. Abhilfe:" >&2
    echo "            git submodule update --init --recursive" >&2
    exit 1
fi

echo "Patches aus lib/asksinpp/patches/ anwenden ..."

# --ignore-whitespace ist nicht Bequemlichkeit, sondern noetig: die Patches
# liegen per .gitattributes immer mit LF vor, der Arbeitsbaum des Submoduls
# dagegen unterliegt dessen eigenem core.autocrlf -- auf Windows also CRLF.
# Ohne die Option findet git apply dort keine einzige Kontextzeile. Die
# Option lockert nur den Vergleich am Zeilenende; der Inhalt der Treffer
# bleibt exakt.
APPLY="--ignore-whitespace"

found=0
for patch in "$patchdir"/*.patch; do
    [ -e "$patch" ] || continue
    found=1
    name=$(basename -- "$patch")
    if git -C "$upstream" apply $APPLY --reverse --check "$patch" >/dev/null 2>&1; then
        echo "  bereits angewandt : $name"
    elif git -C "$upstream" apply $APPLY --check "$patch" >/dev/null 2>&1; then
        git -C "$upstream" apply $APPLY "$patch"
        echo "  angewandt         : $name"
    else
        echo "FEHLER: $name passt nicht auf den Arbeitsbaum." >&2
        echo "" >&2
        # Die Meldung von git selbst ausgeben, nicht wegwerfen -- ohne sie
        # ist nicht zu unterscheiden, ob der Commit nicht stimmt, die Datei
        # fehlt oder die Zeilenenden nicht passen.
        echo "        git sagt dazu:" >&2
        git -C "$upstream" apply $APPLY --check "$patch" 2>&1 | sed 's/^/        /' >&2
        echo "" >&2
        echo "        Steht das Submodul auf dem Commit, den dieses Repo" >&2
        echo "        festhaelt? Pruefen mit:" >&2
        echo "            git submodule status" >&2
        echo "        Zuruecksetzen mit:" >&2
        echo "            git submodule update --force --init --recursive" >&2
        exit 1
    fi
done

if [ "$found" -eq 0 ]; then
    echo "  (keine Patches vorhanden)"
fi

echo ""
echo "Fertig. Bauen mit:"
echo "    UFBT_HOME=~/.ufbt-unleashed ufbt"
