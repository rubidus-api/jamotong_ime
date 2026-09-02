#!/bin/sh
# 저장소가 내놓아도 되는 상태인가 --- 지금은 공개 규율 한 가지만 본다.
set -eu

fail() {
  printf '%s\n' "project-check: $*" >&2
  exit 1
}

git status --short >/dev/null 2>&1 || fail "not a git repository or git is unavailable"

# 이 기계의 절대 경로·인증서가 저장소에 들어갔는가 (작업공간 공용 검사).
# 도구는 저장소 밖(usr/bin)에 있다 --- 없으면 조용히 건너뛴다. 그 자리에서 옳은
# 문자열은 저장소 뿌리의 .privacy-allow 에 적는다.
privacy="$(cd "$(dirname "$0")/.." && pwd)/../usr/bin/check-privacy"
if [ -x "$privacy" ]; then
  "$privacy" || fail "local paths or credentials in the repository"
fi

printf '%s\n' "project-check: ok"
