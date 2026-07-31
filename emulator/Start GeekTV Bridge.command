#!/bin/zsh

clear
cd "/Users/bairachnyi/Documents/Mini PC" || exit 1

echo "GeekTV GitHub Bridge"
echo "===================="
echo

if /usr/bin/nc -z 127.0.0.1 8788 2>/dev/null; then
  echo "Bridge is already running."
  echo "Dashboard: http://localhost:8788"
  /usr/bin/open "http://localhost:8788"
  echo
  read "?Press Enter to close this window..."
  exit 0
fi

echo "Starting bridge..."
echo "Device feed: http://192.168.1.139:8788/api/github"
echo
echo "Keep this window open while GitHub monitoring is needed."
echo "Close the window or press Control+C to stop the bridge."
echo

(
  sleep 2
  /usr/bin/open "http://localhost:8788"
) &

exec /opt/homebrew/bin/node emulator/server.mjs
