#!/bin/sh
set -eu
helper=$1
fixture=$(mktemp -d)
trap 'rm -rf "$fixture"' EXIT HUP INT TERM
mkdir "$fixture/bin" "$fixture/work space"
cat > "$fixture/bin/dbus-send" <<'STUB'
#!/bin/sh
printf '%s\n' "$@" > "$ARGS_FILE"
exit "$MOCK_STATUS"
STUB
chmod +x "$fixture/bin/dbus-send"
export ARGS_FILE="$fixture/args" PATH="$fixture/bin:$PATH"
cd "$fixture/work space"
MOCK_STATUS=0; export MOCK_STATUS
sh "$helper" > "$fixture/out" 2> "$fixture/err"
grep -Fx 'string:terminal_manager' "$fixture/args" > /dev/null
grep -Fx "string:$PWD" "$fixture/args" > /dev/null
grep -Fx 'Terminal launch requested' "$fixture/out" > /dev/null
test ! -s "$fixture/err"
MOCK_STATUS=23; export MOCK_STATUS
status=0
sh "$helper" > "$fixture/out" 2> "$fixture/err" || status=$?
test "$status" -eq 23
test ! -s "$fixture/out"
grep -F 'Could not request terminal launch' "$fixture/err" > /dev/null
