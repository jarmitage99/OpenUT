# OpenUT

Open-source reimplementation of Unreal Tournament. It runs the
original game UnrealScript bytecode from the shipped `.u` packages
on a new C++ engine built on SDL2 and OpenGL 3.3.

The original game data is required and is not included.

## Features

- Runs natively on Linux, Windows, macOS and in the browser
- Original UWindow menus, HUD, bots and game types
- Widescreen and ultrawide support
- imgui-based UnrealEd

Optional cool graphics:

- Anisotropic filtering, trilinear filtering
- TTF fonts
- Soft particles
- MSAA and FXAA
- UI scale for the menus and console
- Bicubic filtering of lightmaps
- Detail textures visible at longer distances

## Requirements

Debian/Ubuntu with:

    $ sudo apt install build-essential cmake pkg-config libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxkbcommon-dev libgl-dev libegl-dev libasound2-dev libpulse-dev libwayland-dev wayland-protocols libdecor-0-dev libssl-dev

Every other library dependency is vendored in `vendor/` and built with the project.

## Game data

Copy the contents of an installed Unreal Tournament (version 436) into
`game-data/`, so that `game-data/System`, `game-data/Maps`,
`game-data/Textures`, `game-data/Sounds` and `game-data/Music` exist.

You can also specify a custom loation with `-DOPENUT_GAME_DATA=/path/to/ut`

The free demo (version 338) also runs. `scripts/extract-demo.sh` unpacks the
files the game needs from `utdemo338.exe` into `demo-data/`, which is the
default data for the Docker browser build. Point `OPENUT_GAME_DATA` at it to
play it natively.

At runtime the game and the editor look for the data in this order:

- `OPENUT_GAME_DATA` environment variable
- `game-data` directory next to the executable (or to the AppImage)
- Steam install of Unreal Tournament `steamapps/common/Unreal Tournament`
- Path given at configure time

## Building

    $ cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    $ cmake --build build -j"$(nproc)"

Options:

- `-DSKIP_QA=ON` skips clang-format during the build.
- `-DSKIP_UT=ON` skips building the unit tests.

`scripts/check-debian.sh` builds the project in clean Debian 12 and Debian 13
containers with Docker.

### Windows

Windows builds are cross-compiled with MinGW:

    $ cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-toolchain.cmake
    $ cmake --build build-mingw -j"$(nproc)"

## Browser builds

Requires Emscripten and WebGL 2. The game data given at configure time is
packaged into `index.data`, without the Windows binaries:

    $ emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release -DSKIP_UT=ON -DOPENUT_GAME_DATA="$PWD/game-data"
    $ cmake --build build-web --target openut -j"$(nproc)"
    $ python3 -m http.server -d build-web/bin 8000

Then open `http://localhost:8000/`. The game data is LZ4 compressed
and cached in IndexedDB after the first download, together with settings.

## Running

    $ build/bin/openut

You can specify a map to be loaded with:

    $ build/bin/openut "DM-Deck16][?game=Botpack.DeathMatchPlus"

Controls and settings live in `~/.local/share/openut` (`%APPDATA%\openut` on
Windows): `UnrealTournament.ini` and `User.ini` are read from there when
present and otherwise from the game `System` dir.

## Multiplayer

In the menu, Multiplayer > Server Browser lists the servers on the local
network, and its Direct Connect tab joins a server by address
(`192.168.1.20` or `192.168.1.20:7777`).

From the CLI:

    $ build/bin/openut "DM-Deck16][?game=Botpack.DeathMatchPlus?listen"
    $ build/bin/openut 192.168.1.20:7777

Dedicated server:

    $ build/bin/openut --server "DM-Deck16][?game=Botpack.DeathMatchPlus"

Servers use UDP port 7777, or `Port` in the `[URL]` section of
`UnrealTournament.ini`, or `--port`. Dedicated servers tick at
`NetServerMaxTickRate` and send each client at most `MaxClientRate` bytes per
second, both from the `[IpDrv.TcpNetDriver]` section. LAN discovery uses UDP
port 8777 and the port after the game port.

Wont work with the original clients or servers, OpenUT uses its own network
protocol. There is no master server either.

The web admin is off by default. To turn it on, set these in
`UnrealTournament.ini`:

    [UWeb.WebServer]
    bEnabled=True
    ListenPort=8080

    [UTServerAdmin.UTServerAdmin]
    AdminUsername=admin
    AdminPassword=change-me

Then open `http://server:8080/ServerAdmin/`. It uses plain HTTP with basic
authentication, so change the password and keep the port off the public
internet.

## Editor

    $ build/bin/openut-editor "DM-Deck16]["

The editor is currently a work in progress. But it works!

## UnrealScript compiler

`ucc` exports and compiles UnrealScript, like the UCC that shipped with the
original game. Sources and compiled packages live in `~/.local/share/openut`
and packages in `~/.local/share/openut/System` are loaded in place of the
shipped ones with the same name.

To export the sources of a package:

    $ build/bin/ucc batchexport Botpack.u class uc ~/.local/share/openut/Botpack/Classes

Compile:

    $ build/bin/ucc make

## Tests

    $ build/bin/unit-tests

Unit tests that need the game data are tagged `[data]`. You can run the ones
that dont need game data with:

    $ build/bin/unit-tests "~[data]"

## Thanks

This project would not be possible without the incredible work from these
legends:

- gildor
- dpJudas
- jmarshall
- AnthraX
- Marco888
