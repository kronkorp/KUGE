# Catch, and a mod next to it

A game of 250 lines (`Catch.hpp`, `main.cpp`) made to be modded: things fall, you catch them with a paddle.
The score is in the title of the window. The whole story of mods and scripts is [chapter 20](../../docs/20-scripts/README.md);
this is the example it refers to.

```
build/example/kuge_modding              # the game with the mod in mods/golden
build/example/kuge_modding --no-mods    # the game alone
```

Left/Right or A/D move, Esc quits. (`--frames 400 --screenshot catch.ppm` plays with nobody at the keyboard and keeps a picture:
that is what the test `modding_smoke` does, with and without the mod.)

## What a mod can change

```
data/catch.json                    the game's numbers
mods/golden/
    mod.json                       { "name": "golden", "version": "1.0", "kuge": "0.3", "depends": [] }
    data/catch.json                { "playerSpeed": 450 }          replaces the game's file: you are faster
    scripts/golden.lua             a Lua script: every 5 seconds a gold coin that falls fast and is worth 5
```

1. **Data, no code.** A mod's `data/catch.json` is looked at before the game's (`AssetRoots`, `Mods::apply`). The game reads
   a key that is missing with its default, so a mod only writes what it changes.
2. **A script, in a sandbox.** `Mods::scripts()` lists the `scripts/*.lua` of the mods, the game loads them into a
   `ScriptEngine` and a `ScriptSystem` calls `update(dt)` at each tick. The script sees **only what the game described**
   (`makeSetup` in `Catch.hpp`): `Transform2D` and `Faller`. It cannot touch the paddle, the score or the window, and it has no `io`, no `os`.
   The game's own `Dress` system draws every `Faller`, so the script does not need to know about sprites.

```lua
function update(dt)
    timer = timer + dt
    if timer >= 5 then
        timer = 0
        world:spawn{ Transform2D = {position = {math.random(-280, 280), -250}}, Faller = {speed = 240, value = 5} }
    end
end
```

## Try

- Change `"playerSpeed"` in `mods/golden/data/catch.json`, run again.
- In `golden.lua`, change `EVERY` and `value`; a mistake that stops the script from loading is said with its file and line, and the
  game does not start (the log shows it before any window). A script that fails *while running* is stopped and logged once, and the game goes on.
- Copy `mods/golden` to `mods/mine`, and make your own: `"depends": ["golden"]` loads it after.
- In multiplayer the server would refuse a client whose mods differ (`Mods::fingerprint()`): see [Scripts](../../docs/20-scripts/README.md).
