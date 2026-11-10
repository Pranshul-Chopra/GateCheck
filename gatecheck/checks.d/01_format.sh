#!/bin/sh
payload=$(cat)

if [ -z "$payload" ]; then
    echo "empty input"
    exit 1
fi

cr=$(printf '\r')
nl=$(printf '\n')
case "$payload" in
    *"$cr"*|*"$nl"*)
        echo "newline or CR log injection detected"
        exit 1
        ;;
esac

echo "format ok"
exit 0
