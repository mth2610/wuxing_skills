#!/bin/bash
cd "$(dirname "$0")"
PORT=8765
# Kill any previous server on this port
lsof -ti :$PORT | xargs kill -9 2>/dev/null
python3 -m http.server $PORT >/dev/null 2>&1 &
PID=$!
sleep 0.5
open "http://localhost:$PORT/index.html"
echo "Wuxing Animation Manager running at http://localhost:$PORT/index.html (PID $PID). Close this window to stop."
wait $PID
