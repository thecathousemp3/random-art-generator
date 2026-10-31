# Quickstart

## Requirements

- CMake 3.16+
- A C++17 compiler (Visual Studio Build Tools on Windows, Xcode Command Line
  Tools on macOS, or `g++`/`clang++` on Linux)
- **macOS/Linux GUI:** Qt 6 with the Widgets and Concurrent modules

On Windows, a C++17 compiler is required (for example Visual Studio Build Tools
with the **Desktop development with C++** workload). The quickstart launcher
uses the native Windows GUI and needs no separate GUI toolkit. On macOS/Linux,
Qt 6 is required for the GUI. The Windows launcher searches common locations for
CMake but does not install missing tools.

## Fastest path

From the project folder:

- **Windows:** run `quickstart.bat` from Command Prompt or PowerShell. It searches
  `PATH`, common CMake install folders, and Visual Studio's bundled CMake.
- **macOS/Linux:** run `sh quickstart.sh` from a terminal.

Each script configures a Release build, builds the program, and opens the GUI.
The canvas starts at the primary display's pixel resolution. Choose **Save
image…** to export PNG or JPEG. Press **Generate art** for a fresh composition
each time. Choose Random, Warm, Cool, Earthy, Neon, or Monochrome to guide the
palette while keeping each render new. Press **Spacebar** to generate again.
The interface uses a monochrome, squared-off pixel-workbench style.

## Build and customize manually

Configure and build:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The Windows app uses native Win32 controls. macOS/Linux builds use Qt 6.
To generate a 4K PNG with a repeatable seed from the command line:

```sh
# Windows PowerShell
.\build\Release\random_art_generator.exe --width 3840 --height 2160 --seed 42 --output art.png

# macOS/Linux
./build/random_art_generator --width 3840 --height 2160 --seed 42 --output art.png
```

Choose a palette with `--palette random`, `warm`, `cool`, `earthy`, `neon`, or
`monochrome`. In the GUI, each generation still creates a fresh composition and
fresh colors within the selected palette style.

The GUI detects the primary display's pixel resolution (Windows and macOS, and
Linux when built with X11 development libraries and run in an X11 session). Set
the width and height in the window to override it. The CLI accepts `--width` and
`--height`; headless systems, Linux systems without X11 support, and mobile
platforms must provide both dimensions explicitly. Use the same seed,
dimensions, and generator version to reproduce an image. PNG works without
extra libraries. JPEG output from the CLI requires libjpeg at build time. The
GUI saves JPEG at quality 98. Rendering uses CPU threads; GPU rendering is not
available.

For all options, run the executable with `--help`. Images are limited to
300 million pixels to reduce the risk of exhausting memory.
