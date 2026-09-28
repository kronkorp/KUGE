#!/bin/sh
# Plays R-Type for real, twice, on SDL's dummy screen:
#   1. a dedicated server (its own process) and a client that reaches it over the network;
#   2. the host: the room and the window in one process.
# Each client is a scripted pilot: it must join a game, see it, and leave a picture with the game in it.
#
#     RunRType.sh <server> <client> <host> <outdir>

SERVER="$1"; CLIENT="$2"; HOST="$3"; OUT="$4"
export SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
PORT=$((20000 + $$ % 20000))
mkdir -p "$OUT"
fail() { echo "rtype smoke: $1"; [ -n "$SPID" ] && kill "$SPID" 2>/dev/null; exit 1; }

# The picture is 960x540: a header of 15 bytes, then the pixels. The game must be in it: the ship (white),
# an enemy (red), a bullet (yellow).
check_picture() {
    size=$(wc -c < "$1")
    [ "$size" -eq $((15 + 960 * 540 * 3)) ] || fail "$1 is $size bytes"
    hex=$(od -An -tx1 -v "$1" | tr -d ' \n')
    for colour in ffffff e64646 fff078; do
        echo "$hex" | grep -q "$colour" || fail "the colour $colour is not in $1: the game was not drawn"
    done
}

"$SERVER" "$PORT" > "$OUT/server.log" 2>&1 &
SPID=$!
sleep 1
"$CLIENT" --port "$PORT" --frames 420 --screenshot "$OUT/client.ppm" > "$OUT/client.log" 2>&1 || { cat "$OUT/client.log"; fail "the client did not join a game and see it"; }
kill -INT "$SPID"
wait "$SPID" || fail "the server did not stop cleanly"
SPID=""
grep -q "room 1 is closed" "$OUT/server.log" || fail "the room was not left when the server stopped"
check_picture "$OUT/client.ppm"

"$HOST" --frames 420 --screenshot "$OUT/host.ppm" > "$OUT/host.log" 2>&1 || { cat "$OUT/host.log"; fail "the host did not join its own game and see it"; }
check_picture "$OUT/host.ppm"
echo "rtype smoke: ok"
