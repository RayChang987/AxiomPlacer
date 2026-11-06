#!/usr/bin/env bash
# Run AxiomPlacer and follow the optimisation live in the browser.
#
#   tools/live.sh benchmarks/large_110.txt --time 60
#
# Starts a local web server for the viewer, prints its URL and runs the placer
# with --trace. Results land in runs/<name>.{out,svg,jsonl}.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="${AXIOM_BIN:-$ROOT/build/axiom-placer}"
PORT="${PORT:-8000}"

if [[ $# -lt 1 ]]; then
  echo "usage: $0 <problem.txt> [axiom-placer options...]" >&2
  exit 2
fi
if [[ ! -x "$BIN" ]]; then
  echo "placer not found at $BIN - build it first (cmake -B build && cmake --build build)" >&2
  exit 1
fi

IN="$1"
shift
NAME="$(basename "${IN%.*}")"
mkdir -p "$ROOT/runs"
TRACE="runs/$NAME.jsonl"
: >"$ROOT/$TRACE"

python3 -m http.server "$PORT" --bind 127.0.0.1 --directory "$ROOT" >/dev/null 2>&1 &
SERVER=$!
trap 'kill $SERVER 2>/dev/null || true' EXIT

echo "viewer: http://127.0.0.1:$PORT/viewer/?trace=../$TRACE&live=1"
"$BIN" "$IN" "$ROOT/runs/$NAME.out" --trace "$ROOT/$TRACE" --svg "$ROOT/runs/$NAME.svg" "$@"
echo "finished - the viewer stays available until you press Ctrl+C"
wait "$SERVER"
