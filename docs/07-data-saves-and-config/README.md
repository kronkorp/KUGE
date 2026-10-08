# 07 Data, saves and config

Everything that leaves memory in KUGE goes through **one** serializer: saves, config, keybinds, network
messages. Learning it once serves the four.

## The serializer: `ByteWriter` and `ByteReader`

Binary, **little-endian**, the same on every machine. Numbers have their exact size (an `int` is 4 bytes, a
`double` 8), a string is its length as 4 bytes then its characters.

```cpp
kuge::ByteWriter out;

out.write<std::uint32_t>(42);
out.write(1.5f);
out.write<bool>(true);
out.writeString("Ana");
out.writeVector(std::vector<std::uint16_t>{1, 2, 3});

std::vector<std::uint8_t> bytes = out.bytes();

kuge::ByteReader in(bytes);                       // reads from a buffer that must outlive it
const auto n = in.read<std::uint32_t>();
const auto x = in.read<float>();
const bool b = in.read<bool>();
const auto name = in.readString();
const auto list = in.readVector<std::uint16_t>();
```

**Every read is checked.** Reading past the end, a `bool` that is neither 0 nor 1, a length that the data cannot
hold: all throw `SerializerError` instead of returning garbage or allocating gigabytes. It is safe to read data
from an untrusted peer or a damaged file: the worst case is an exception you catch.

Two guardrails in the API:

- `ByteReader in(saved())` on a **temporary** vector does not compile (the reader would read freed memory).
- Versioned headers: `writeHeader(magic, version)` / `readHeader(magic)`, and `fourcc('K','S','A','V')` makes a
  magic number.

`writeFile(path, bytes)` is **atomic** (a temporary file, then a rename): a crash never leaves half a file.
`readFile(path)` reads one.

## Config files

`ConfigFile` reads and writes `key = value` text, with `[sections]`, for settings a human edits.

```cpp
kuge::ConfigFile config;

config.load("settings.cfg");                                   // false if there is no such file
const auto fullscreen = config.getBool("video.fullscreen", false);
const auto volume = config.getDouble("audio.volume", 0.8);      // typed reads with a fallback
const auto name = config.getString("player.name", "pilot");
const auto width = config.getInt("video.width", 1280);

config.set("audio.volume", 0.5);
config.save("settings.cfg");                                   // alphabetical, atomic
```

A mistake in a file names its line. The keybinds file is a config file too (`input.fire = Space, Pad.A`).

## Where the player's files go

```cpp
const auto settings = kuge::userDirectory(kuge::UserDir::Config, "mygame");   // ~/.config/mygame
const auto saves    = kuge::userDirectory(kuge::UserDir::Data,   "mygame");   // ~/.local/share/mygame
```

(`$XDG_CONFIG_HOME` and `$XDG_DATA_HOME` are honoured; the folder is created. On Windows, both are
`%APPDATA%\mygame`.)

## Save slots

`SaveSlots` keeps bytes in **named slots**, one `.ksave` file each, with a header that makes a bad save
recognisable:

```cpp
kuge::SaveSlots saves(kuge::userDirectory(kuge::UserDir::Data, "mygame"), "mygame", /*version*/ 3);

saves.write("slot1", "Level 3 - 12 min", writer.bytes());       // a label for the menu
for (const auto& entry : saves.list()) { /* fill a "Continue" menu; damaged ones say why */ }

try {
    const auto data = saves.read("slot1");                        // data.payload, data.info.version
} catch (const kuge::SaveError& error) {
    switch (error.reason()) {
        case kuge::SaveError::Reason::Missing:   ...
        case kuge::SaveError::Reason::Corrupt:   ...   // the CRC-32 does not match
        case kuge::SaveError::Reason::WrongGame: ...
        case kuge::SaveError::Reason::TooNew:    ...   // written by a newer version of the game
        default: ...
    }
}
```

A file holds the game's name, the version that wrote it, a label, the time and a CRC-32. An older version is
read and its version given, so the game can convert what it holds. Writes are atomic, so a crash while saving
keeps the previous save.

## Snapshots of a world

To save *a World*, say what to keep and how, with a `SnapshotRegistry`:

```cpp
kuge::SnapshotRegistry registry;

registry.component<Health>("health",
    [](kuge::ByteWriter& out, const Health& h) { out.write(h.points); },
    [](kuge::ByteReader& in) { return Health{in.read<int>()}; });
registry.resource<Score>("score", ...);

world.add<kuge::Persistent>(hero, {});                  // this entity is saved

kuge::ByteWriter out;
registry.save(world, out);                              // then give out.bytes() to a SaveSlots
registry.load(world, reader);                           // creates the entities again
```

- **Nothing is saved that was not registered**, so a save never holds a texture or a pointer by accident.
- Entities go by increasing number and components in registration order: the same world always gives the same bytes.
- A component **name** that the reader does not know is skipped, so a save from a newer game version still loads.
- **Loading is all or nothing:** the data is checked first, and a damaged save changes nothing.
- Loading creates *new* entities: a component must not hold the number of another entity.

The platformer's Save game / Continue is the complete example (`example/platformer/Platformer.hpp`).

## Which one for what

| You want to... | Use |
|---|---|
| let the player edit settings by hand | `ConfigFile` |
| keep a game in progress | `SnapshotRegistry` + `SaveSlots` |
| put a value on the wire | the same `ByteWriter`, through `KUGE_MESSAGE` ([08](../08-the-network/README.md)) |
| load a level | `TileMap::load` (text format, see the README) |

Next: [08 The network](../08-the-network/README.md).
