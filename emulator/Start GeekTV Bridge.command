#!/bin/zsh

clear

PROJECT_DIR="/Users/bairachnyi/Documents/Mini PC"
NODE_BIN="/opt/homebrew/bin/node"
DEVICE_HOST="${GEEKTV_DEVICE_HOST:-geektv.local}"
KEYCHAIN_SERVICE="GeekTV Bridge device admin"

cd "$PROJECT_DIR" || {
  echo "Project folder was not found: $PROJECT_DIR"
  read "?Press Enter to close..."
  exit 1
}

echo "GeekTV GitHub Bridge"
echo "===================="
echo

BRIDGE_ALREADY_RUNNING=false
if /usr/bin/nc -z 127.0.0.1 8788 2>/dev/null; then
  BRIDGE_ALREADY_RUNNING=true
  echo "Bridge is already running; checking the current network and device..."
  echo
fi

# Prefer the active default interface, then fall back to the usual Mac ports.
DEFAULT_IF=$(/sbin/route -n get default 2>/dev/null | /usr/bin/awk '/interface:/{print $2; exit}')
LAN_IP=""
if [[ -n "$DEFAULT_IF" ]]; then
  LAN_IP=$(/usr/sbin/ipconfig getifaddr "$DEFAULT_IF" 2>/dev/null)
fi
[[ -z "$LAN_IP" ]] && LAN_IP=$(/usr/sbin/ipconfig getifaddr en0 2>/dev/null)
[[ -z "$LAN_IP" ]] && LAN_IP=$(/usr/sbin/ipconfig getifaddr en1 2>/dev/null)

if [[ -z "$LAN_IP" ]]; then
  echo "Could not determine the Mac LAN address. Connect to the same network as GeekTV."
  read "?Press Enter to close..."
  exit 1
fi

FEED_URL="http://${LAN_IP}:8788/api/github"
DEVICE_IP=$(/usr/bin/dscacheutil -q host -a name "$DEVICE_HOST" 2>/dev/null | /usr/bin/awk '/ip_address:/{print $2; exit}')

echo "Mac address:    $LAN_IP"
echo "Device feed:   $FEED_URL"
if [[ -n "$DEVICE_IP" ]]; then
  echo "GeekTV device: $DEVICE_IP ($DEVICE_HOST)"
else
  echo "GeekTV device: not found yet ($DEVICE_HOST)"
fi
echo

# Keep the local settings preview synchronized with the address used by the
# physical device. This file is private and ignored by git.
if [[ -f emulator/device-config.local.json ]]; then
  TMP_CONFIG="${TMPDIR:-/tmp}/geektv-device-config.$$.json"
  if /usr/bin/jq --arg url "$FEED_URL" '.github.statusUrl=$url | .github.pollSec=10' \
      emulator/device-config.local.json > "$TMP_CONFIG" 2>/dev/null; then
    /bin/mv "$TMP_CONFIG" emulator/device-config.local.json
  else
    /bin/rm -f "$TMP_CONFIG"
  fi
fi

if [[ "$BRIDGE_ALREADY_RUNNING" == "false" ]]; then
  echo "Starting bridge..."
  "$NODE_BIN" emulator/server.mjs &
  BRIDGE_PID=$!
  trap 'kill "$BRIDGE_PID" 2>/dev/null' INT TERM EXIT
fi

for _ in {1..40}; do
  /usr/bin/nc -z 127.0.0.1 8788 2>/dev/null && break
  /bin/sleep 0.25
done

if ! /usr/bin/nc -z 127.0.0.1 8788 2>/dev/null; then
  echo
  echo "Bridge did not start. Review the error above."
  [[ -n "$BRIDGE_PID" ]] && wait "$BRIDGE_PID"
  exit $?
fi

# A first live refresh can take longer than the ESP8266 HTTP timeout because
# the bridge has to inspect many repositories. Warm the cache before pointing
# the device at this Mac, so startup does not briefly show Bridge offline.
echo "Loading current GitHub data..."
if ! /usr/bin/curl -sS --max-time 90 http://127.0.0.1:8788/api/github >/dev/null; then
  echo "GitHub data is still loading; the device will retry automatically."
fi

login_device() {
  local password="$1"
  local body
  body=$(/usr/bin/jq -cn --arg pass "$password" '{pass:$pass}')
  /usr/bin/curl -sS --max-time 5 -H 'Content-Type: application/json' \
    -d "$body" "http://${DEVICE_IP}/api/login" 2>/dev/null | \
    /usr/bin/jq -e '.ok == true' >/dev/null 2>&1
}

if [[ -n "$DEVICE_IP" ]]; then
  DEVICE_PASS=$(/usr/bin/security find-generic-password -a "$DEVICE_IP" -s "$KEYCHAIN_SERVICE" -w 2>/dev/null)
  [[ -z "$DEVICE_PASS" ]] && DEVICE_PASS="1111"

  if ! login_device "$DEVICE_PASS"; then
    echo
    echo "Device login is required to update its Bridge address."
    read -s "?GeekTV admin password: " DEVICE_PASS
    echo
    if login_device "$DEVICE_PASS"; then
      /usr/bin/security add-generic-password -U -a "$DEVICE_IP" -s "$KEYCHAIN_SERVICE" -w "$DEVICE_PASS" >/dev/null 2>&1
    else
      DEVICE_PASS=""
      echo "Could not log in. Update the GitHub feed URL manually: $FEED_URL"
    fi
  fi

  if [[ -n "$DEVICE_PASS" ]]; then
    CONFIG_BODY=$(/usr/bin/jq -cn --arg url "$FEED_URL" '{github:{statusUrl:$url,pollSec:10}}')
    SAVE_RESULT=$(/usr/bin/curl -sS --max-time 8 -H 'Content-Type: application/json' \
      -d "$CONFIG_BODY" "http://${DEVICE_IP}/api/config" 2>/dev/null)
    if [[ "$(echo "$SAVE_RESULT" | /usr/bin/jq -r '.ok // false' 2>/dev/null)" == "true" ]]; then
      echo "Device configured automatically: $FEED_URL"
    else
      echo "Device was found, but its settings could not be saved."
      echo "Set the GitHub feed manually to: $FEED_URL"
    fi
  fi
else
  echo "No GeekTV was discovered in this network."
  echo "Once the device is online, set its GitHub feed to: $FEED_URL"
fi

echo
echo "Dashboard:      http://localhost:8788"
echo "Device settings: http://localhost:8789/settings.html"
echo
echo "Keep this window open while GitHub monitoring is needed."
echo "Close it or press Control+C to stop the bridge."

(
  /bin/sleep 1
  /usr/bin/open "http://localhost:8788"
) &

if [[ "$BRIDGE_ALREADY_RUNNING" == "true" ]]; then
  echo
  read "?Bridge remains running. Press Enter to close this window..."
else
  wait "$BRIDGE_PID"
fi
