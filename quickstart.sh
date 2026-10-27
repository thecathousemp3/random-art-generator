#!/bin/sh
set -eu

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
if [ -x build/random_art_gui ]; then
    app=build/random_art_gui
elif [ -x build/Release/random_art_gui ]; then
    app=build/Release/random_art_gui
else
    echo "Could not find the built random_art_gui executable." >&2
    exit 1
fi
"$app"
