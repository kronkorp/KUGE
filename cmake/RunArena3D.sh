#!/bin/sh
# Plays Arena for real, twice, on SDL's dummy screen:
#   1. a dedicated server (its own process) and a client that reaches it over the network;
#   2. the host: the room and the window in one process.
# Each client is a scripted player: it must join a game, run, jump and shoot (its character plays each clip, and its
# bullets are seen: the exit code says so), and leave a picture with the game in it.
#
#     RunArena3D.sh <server> <client> <host> <outdir>

SERVER="$1"; CLIENT="$2"; HOST="$3"; OUT="$4"
export SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy
PORT=$((20000 + $$ % 20000))
mkdir -p "$OUT"
rm -f "$OUT/stop"

# Windows cannot send Ctrl+C to another program: there, the server is stopped by "quit" on its standard input,
# written once the file $OUT/stop exists. Elsewhere it gets a SIGINT, as from Ctrl+C.
case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) WINDOWS=1 ;;
    *)                    WINDOWS="" ;;
esac
stop_server() {
    if [ -n "$WINDOWS" ]; then touch "$OUT/stop"; else kill -INT "$SPID"; fi
}
fail() { echo "arena smoke: $1"; [ -n "$SPID" ] && stop_server 2>/dev/null; exit 1; }

# The picture is 640x360: a header of 15 bytes, then the pixels. The game must be in it: the health bar (green) and
# the crosshair (white) are pure colors, and a lit, textured, foggy 3D scene has hundreds of other colors (a flat
# picture would have a few).
check_picture() {
    size=$(wc -c < "$1")
    [ "$size" -eq $((15 + 640 * 360 * 3)) ] || fail "$1 is $size bytes"
    od -An -tx1 -v -w3 "$1" | tr -d ' ' > "$1.colours"
    for colour in 00ff00 ffffff; do
        grep -q "^$colour$" "$1.colours" || fail "the colour $colour is not in $1: the game was not drawn"
    done
    count=$(sort -u "$1.colours" | wc -l)
    [ "$count" -ge 300 ] || fail "$1 has only $count colours: the 3D scene was not drawn"
}

if [ -n "$WINDOWS" ]; then
    ( while [ ! -e "$OUT/stop" ]; do sleep 1; done; echo quit ) | "$SERVER" "$PORT" > "$OUT/server.log" 2>&1 &
else
    "$SERVER" "$PORT" > "$OUT/server.log" 2>&1 &
fi
SPID=$!
sleep 1
"$CLIENT" --port "$PORT" --frames 420 --screenshot "$OUT/client.ppm" > "$OUT/client.log" 2>&1 || { cat "$OUT/client.log"; fail "the client did not join a game, run, jump and shoot"; }
stop_server
wait "$SPID" || fail "the server did not stop cleanly"
SPID=""
grep -q "room 1 is closed" "$OUT/server.log" || fail "the room was not left when the server stopped"
check_picture "$OUT/client.ppm"

"$HOST" --frames 420 --screenshot "$OUT/host.ppm" > "$OUT/host.log" 2>&1 || { cat "$OUT/host.log"; fail "the host did not join its own game, run, jump and shoot"; }
check_picture "$OUT/host.ppm"
echo "arena smoke: ok"
