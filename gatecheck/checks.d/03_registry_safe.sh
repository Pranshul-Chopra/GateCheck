#!/bin/sh
payload=$(cat)

if printf '%s' "$payload" | grep -qiE '(<script|drop[[:space:]]+table|\$\{jndi:)'; then
    echo "suspicious injection signature"
    exit 1
fi

echo "payload clean"
exit 0
