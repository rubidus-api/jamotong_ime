#!/bin/sh
# wine 에서 probe_host 를 돌려 results/ 에 남긴다.
# wine 결과는 "호출 형식이 맞는가"를 싸게 거르는 용도다 — 계약의 참거짓은 Windows 실기에서만.
set -e
here=$(cd "$(dirname "$0")/.." && pwd)
out="$here/results/wine-$(date +%Y%m%d-%H%M).txt"

: "${WINEPREFIX:=$HOME/.wine-tsflab}"
export WINEPREFIX
export WINEDLLOVERRIDES="mscoree,mshtml="
export WINEDEBUG="${WINEDEBUG:--all}"
mkdir -p "$here/results"

{
  echo "host: wine $(wine --version 2>/dev/null)"
  echo "date: $(date -Is)"
  echo "prefix: $WINEPREFIX"
  echo
  wine "$here/probe_host.exe" 2>&1
} | tee "$out"

echo "saved: $out"
