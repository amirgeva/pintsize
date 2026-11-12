# Pint-Size Prix

![Pint-Size Prix gameplay](docs/intro.gif)

A top-down miniature racing game written in C++17 with SDL2. Thumb-sized vehicles race across everyday places: a breakfast table, a woodworking bench, a freezer drawer, a sink full of dishes, an office desk and a backyard sandbox.

The game is built for the keyboard, including two players sharing one. All artwork and sound are generated in code when the game runs, so the repository contains no asset files.

## Contents

- [Features](#features)
- [Building](#building)
- [How to play](#how-to-play)
- [Project layout](#project-layout)
- [How it works](#how-it-works)
- [Tuning and extending](#tuning-and-extending)
- [Testing](#testing)
- [License](#license)

## Features

- Six courses, each a real-life environment with its own vehicle
- Solid obstacles at believable scale: plates, bottles, saws, books, saucepans, sandcastles
- Momentum-based handling: cars rotate directly and slide before they grip
- Table edges you can fall from, jumps across gaps, and spills that change your grip
- Tournament, head-to-head (against the CPU or a second player) and time trial modes
- Computer drivers that plan their own line around whatever is on the course
- Synthesised engine, skid and impact sounds, plus a menu tune

## Building

### Requirements

- CMake 3.16 or newer
- A C++17 compiler
- SDL2, which can come from vcpkg or a system package, or be downloaded automatically by CMake

The game is developed and tested on Windows. It uses only SDL2 and the standard library, but the Linux and macOS steps below have not been tested yet.

### Windows with Visual Studio 2022 and vcpkg

The `vcpkg.json` manifest installs SDL2 into the build folder, leaving your global vcpkg tree alone. This assumes `VCPKG_ROOT` points at your vcpkg checkout.

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" `
      -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build build --config Release
.\build\Release\pintsize.exe
```

`SDL2.dll` is copied next to the executable. Keep the two together if you move the game.

### Without vcpkg

If CMake cannot find an installed SDL2, it downloads SDL 2.30.9 and links it statically:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
.\build\Release\pintsize.exe
```

Pass `-DPSP_FORCE_FETCH_SDL=ON` to use the downloaded copy even when SDL2 is installed.

### Linux and macOS

```sh
# Debian or Ubuntu
sudo apt install build-essential cmake libsdl2-dev
# macOS
brew install cmake sdl2

cmake -S . -B build
cmake --build build -j
./build/pintsize
```

### Debian package

`scripts/build-deb.sh` builds the game and packages it as a Debian source package (`.dsc`, `.orig.tar.gz`, `.debian.tar.xz`) and a binary `.deb`, using the packaging in `debian/`:

```sh
scripts/build-deb.sh --install-deps   # first time: installs debhelper etc. with sudo
scripts/build-deb.sh                  # results go to build/deb/
sudo apt install ./build/deb/pintsize_*_amd64.deb
```

Run `scripts/build-deb.sh --help` for options such as `--binary-only` and `--sign`. The package links against the system SDL2 and never downloads anything. Its library version requirements come from the system it is built on, so build it on the oldest Debian or Ubuntu release you want to support, or in a clean chroot with `sbuild` or `pbuilder`.

## How to play

### Controls

|               | Player 1     | Player 2   |
|---------------|--------------|------------|
| Steer         | Left / Right | A / D      |
| Accelerate    | Up           | W          |
| Brake/reverse | Down         | S          |
| Fire (tanks)  | Right Shift  | Left Shift |

When you play alone, either set works, and X (accelerate), Z (brake) and Space or either Shift (fire) are also available.

| Key                 | Action                                    |
|---------------------|-------------------------------------------|
| Arrow keys          | Move through menus                        |
| Enter or Space      | Confirm                                   |
| Esc                 | Go back, or pause during a race           |
| M                   | Mute                                      |
| F11                 | Toggle fullscreen                         |

Steering is linear: the angle you turn is proportional to how long you hold the key, and it works even when the car is standing still. A car keeps its momentum after you turn, so expect to slide, especially on low-grip vehicles and surfaces.

### Game modes

**Tournament.** Race three computer cars through all six courses in order. Finish 1st or 2nd to advance. Any lower finish costs one of your three lives and you retry the course. Three wins in a row earn an extra life. The computer cars get better each round.

**Head to head.** Two cars share one screen, which follows whoever is ahead. Each player starts with 4 of the 8 points. Leave your rival behind the edge of the screen and you take a point from them, then both cars restart together at the leader's position. Collect all 8 to win outright. Otherwise, whoever has more points when the leader completes the course's laps wins, and a tie goes to sudden death, where the next point decides it. Play against the CPU or against a second player on the same keyboard.

**Time trial.** Drive alone on any course. Your best lap for each course is saved in SDL's preference folder for `PintSize/Prix` (on Windows, `%APPDATA%\PintSize\Prix\best.dat`).

### Courses

| Course          | Environment       | Vehicle | Laps | Watch out for                                  |
|-----------------|-------------------|---------|------|------------------------------------------------|
| Sunday Brunch   | Kitchen table     | Coupe   | 3    | The table edge, juice and jam spills, cutlery  |
| Sawdust Sprint  | Woodworking bench | Buggy   | 3    | The bench edge, a jump over the tool well, glue |
| Deep Freeze     | Freezer drawer    | Sled    | 3    | Very little grip, ice patches, deep frost      |
| Dishwater Dash  | Kitchen sink      | Skiff   | 3    | Floating dishes, suds, a drain that swallows you |
| Homework Hustle | Office desk       | Formula | 3    | The desk edge, two jumps, ink spills           |
| Backyard Blitz  | Sandbox           | Tank    | 2    | Loose sand beside the lane, enemy fire         |

There is no painted road. Each course marks its route with something that belongs there: a trail of spilled cereal, snapped chalk lines, rows of frozen peas, lines of soap bubbles, highlighter strips, or a packed-down lane edged with pebbles.

### Vehicles

| Vehicle | Accel | Top speed | Turn rate | Grip | Mass | Off-road penalty | Notes                 |
|---------|-------|-----------|-----------|------|------|------------------|-----------------------|
| Coupe   | 360   | 235       | 5.4       | 8.5  | 1.0  | 1.0              | Balanced              |
| Buggy   | 400   | 225       | 5.6       | 6.5  | 1.1  | 0.45             | Shrugs off rough ground |
| Formula | 460   | 290       | 5.2       | 11.0 | 0.85 | 1.3              | Fast and grippy, hates rough ground |
| Skiff   | 300   | 240       | 4.4       | 2.6  | 1.0  | 1.0              | Drifts like a boat    |
| Tank    | 290   | 190       | 3.8       | 16.0 | 2.2  | 0.2              | Heavy, armed, barely slides |
| Sled    | 320   | 230       | 5.0       | 3.6  | 0.9  | 1.0              | Slides through every corner |

Speeds are in pixels per second, acceleration in pixels per second squared, turn rate in radians per second and grip in sideways-velocity decay per second. The player's turn rate is 48% of the listed value (see [Tuning](#tuning-and-extending)).

### Surfaces and hazards

| Surface         | Examples                       | Effect                                             |
|-----------------|--------------------------------|----------------------------------------------------|
| Normal ground   | Tablecloth, wood, water        | Full performance, except in the freezer (75% grip) |
| Rough           | Sawdust, sandpaper, loose sand, suds | Less acceleration and grip, plus drag, scaled by the vehicle's off-road penalty |
| Slick           | Juice, milk, coffee, ink, ice  | 60% acceleration and almost no grip                |
| Sticky          | Jam, glue                      | About half acceleration and heavy drag             |
| Solid           | Plates, bottles, tools, walls  | You bounce off                                     |
| Drop            | Table edges, gaps, the drain   | You fall and are put back on the course            |

Other rules worth knowing:

- Cars bounce hard off each other, so a well-timed shove can send a rival into an obstacle or off the table.
- After a fall you reappear near the last safe spot, in a space clear of other cars and obstacles, and flicker for 1.6 seconds while shells cannot hit you. If you fell at a jump, you restart with a run-up.
- The ground beside the route is open, but going more than about 100 pixels past the route's edge puts you straight back on course.
- A ramp launches you as you leave it, so the faster you hit it the further you fly. Too slow and you land in the gap.
- A tank shell spins its target for 0.8 seconds, and the target is briefly protected from further hits.

## Project layout

```
CMakeLists.txt     Build definition
vcpkg.json         vcpkg manifest (SDL2)
src/
  common.h         Vec2, angle helpers, the Surface enum, view size and tick rate
  pix.h            Colour, noise and signed-distance helpers for the procedural art
  props.h          PROP_LIST: every object and its size, shadow height and solidity
  props.cpp        Per-pixel painters for every object
  tracks.cpp       Vehicle stats, environment settings and the six course layouts
  track.h/.cpp     Builds a course: route, ground, object placement, racing line
  race.h/.cpp      Game rules and physics: cars, collisions, laps, computer drivers
  gfx.h/.cpp       Rendering: world chunks, car sprites, particles, bitmap font
  audio.h/.cpp     Software synthesiser for engines, effects and the menu tune
  game.h/.cpp      Menus, screen flow, HUD, keyboard input, saved best laps
  main.cpp         Window, fixed-timestep loop and the --shot debug tool
tests/
  simtest.cpp      Headless test of every course and game mode
```

The code is split into two layers. `psp_core` (`track`, `tracks`, `props`, `race`) contains the rules and world generation and has no SDL dependency, which is what lets the test program run without a display. The `pintsize` executable adds rendering, sound and input on top.

## How it works

**Game loop.** The simulation advances in fixed 1/60-second ticks through `Race::step`, independently of the display's frame rate. Each frame is drawn to a 480×270 texture and scaled up to fit the window, which keeps the chunky pixel look at any size.

**Handling.** Each tick, a car's velocity is split into forward and sideways parts. Throttle and drag act on the forward part, and grip decays the sideways part, so a car that turns keeps sliding in its old direction until the grip catches up. Surfaces scale acceleration, grip and drag.

**Collisions.** Each car is a box matched to its sprite. Cars are tested against each other with the separating-axis test and pushed apart over several passes per tick, so a push can ripple through a pile of cars, and a car pinned against an object stays put while the other one moves. Objects are tested with probe points around the car's outline, using the surface map to find the true direction out of whatever was hit. A hit bounces both cars, briefly breaks the tyres' grip so the bounce carries them away, and adds spin if it lands off-centre.

**Building a course.** Each course is generated when it loads, in a few stages:

1. Control points are turned into a closed Catmull-Rom spline and resampled every 4 pixels.
2. A distance field records how far each pixel is from the route and which route sample is closest.
3. The base material is painted, along with route markings, ramps and gaps.
4. Objects are laid out, either at fixed positions, relative to the route, or scattered at random with a per-course seed. Scattered clutter is kept clear of the route.
5. Each object paints its shadow and itself, and writes to a surface map that gives every pixel its physical behaviour.
6. A racing line is planned over the finished surface map.

World scale is about 6 pixels per centimetre, so a dinner plate is around 170 pixels across and a car around 20.

**Object art.** Every object is a small analytic painter in `props.cpp`, evaluated per pixel in the object's own coordinates. Signed-distance shapes give the outlines, and simple sphere and cylinder lighting gives the shading, with the light direction rotated to match each object so that shadows and highlights stay consistent across the world.

**Computer drivers.** The racing line starts from an ideal that cuts the inside of bends, then a dynamic-programming search picks the cheapest path across the route's width. Paths near solid objects or drops are effectively forbidden, and rough or slippery ground costs extra, so the line adapts to any layout automatically. Computer cars then drive this line using the same steer, throttle and brake inputs a player has, with a speed profile that brakes ahead of tight sections. In races against a human, their speed is scaled between 90% and 110% depending on the gap, and it is never reduced on the approach to a jump.

**Graphics.** The world is uploaded as a grid of 256×256 textures, and only those in view are drawn. Each vehicle and driver colour gets a pre-rendered sheet of 64 rotation frames.

**Sound.** An SDL audio callback mixes two engine oscillators, a skid noise channel, sixteen one-shot effect voices and a short step-sequenced menu tune.

## Tuning and extending

| What you want to change                 | Where                                                     |
|-----------------------------------------|-----------------------------------------------------------|
| Vehicle speed, grip, weight, hit box    | `vehicleSpec` in `src/tracks.cpp`                         |
| How fast the player's car turns         | `PLAYER_TURN_SCALE` at the top of `src/race.cpp`          |
| Jump arc, bounciness, respawn timing    | Constants at the top of `src/race.cpp`                    |
| Surface effects                         | `surfaceFx` in `src/race.cpp`                             |
| Course layouts and object placement     | `buildTracks` in `src/tracks.cpp`                         |
| Amount and mix of clutter               | `themeDressing` in `src/tracks.cpp`                       |
| Tournament difficulty                   | `Game::startRace` in `src/game.cpp`                       |
| Key bindings                            | `Game::readPlayer` in `src/game.cpp`                      |

### Adding a course

Add a `TrackDef` in `buildTracks`:

- `points` are the route's control points in world pixels. The world is sized around them automatically.
- `jumps` take a route position in control-point units (3.5 is halfway between points 3 and 4) and a gap length. Put them on straight sections with room to build speed.
- `props` use three helpers: `P` places an object at a fixed position, `R` places it relative to the route by position and sideways offset, and `Under` places it beneath the water with no collision.

Then check the result. The test program reports any object blocking the racing line, and this renders the whole world to a bitmap:

```powershell
.\build\Release\pintsize.exe --shot world.bmp map:0
```

The saved best-lap table has room for 16 courses.

### Adding an object

Add a line to `PROP_LIST` in `src/props.h`, within the section for its environment, and add a matching `case` to that environment's painter in `src/props.cpp`. Section order matters, because `shadeProp` uses each object's position in the list to choose a painter. Keep `reach` large enough to cover everything the painter draws, or the object will be clipped.

## Testing

The headless test needs no display or sound device:

```powershell
cd build
ctest -C Release --output-on-failure
```

Run `build\Release\psp_simtest.exe` directly to see the full report. For every course it checks that:

- no starting grid position is inside an object, and the racing line is clear
- computer cars finish at two skill levels
- a bot using the player's steering finishes
- cars never visibly overlap during a race, and staged crashes against cars and objects bounce apart
- head-to-head matches terminate and award points correctly
- a player who never moves loses every point in head-to-head and finishes last in a race

It also confirms that the angle turned is exactly proportional to how long the key is held.

To look at the game without playing it, `--shot` renders one frame off-screen:

```powershell
.\build\Release\pintsize.exe --shot frame.bmp race:0 600
```

The target can be `menu`, `race:N`, `h2h:N` or `map:N`, where `N` is a course number from 0 to 5. The optional last argument is how many ticks to simulate first. In race and head-to-head shots, computer drivers control every car.

## License

Released under the GNU General Public License v3.0. See [LICENSE](LICENSE).

SDL2 is a separate project under the zlib license.
