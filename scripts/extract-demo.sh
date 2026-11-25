#!/bin/sh
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
installer=${1:-$root/reference/utdemo338.exe}
output=${2:-$root/demo-data}

mkdir -p "$output"
7z x -y -o"$output" "$installer" \
    'Maps/*.unr' 'Music/*.umx' 'Sounds/*.uax' 'Textures/*.utx' 'Help/Unreal.ico' \
    'System/*.u' 'System/Default.ini' 'System/DefUser.ini' \
    System/Botpack.int System/Core.int System/Engine.int System/IpDrv.int System/IpServer.int \
    System/SoldierSkins.int System/UBrowser.int System/UMenu.int System/UnrealShare.int \
    System/UnrealTournament.int System/UTMenu.int System/UTServerAdmin.int System/UWeb.int \
    -x!System/Editor.u '-x!Textures/*Demo.utx' -x!Textures/DemoFX.utx -x!Textures/Palettes.utx -x!Sounds/CountDown.uax > /dev/null
