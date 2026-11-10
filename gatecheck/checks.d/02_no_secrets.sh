#!/bin/sh
payload=$(cat)

if printf '%s' "$payload" | grep -qiE '(password|api_key|secret|bearer)[[:space:]]*[:=]'; then
    echo "exposed credential pattern detected"
    exit 1
fi

echo "no exposed secrets"
exit 0
