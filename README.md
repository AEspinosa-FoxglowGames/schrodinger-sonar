# schrodinger-sonar

A puzzle game built from scratch in C++ and OpenGL — no engine — based on
quantum observation mechanics.

The core concept: the world only exists in a defined state when it is observed.
The player can ping a sonar to reveal the environment, but the act of observing
can also cause obstacles to materialise — creating a risk/reward loop inspired
by Schrödinger's thought experiment.

## Status

🚧 Early development — core mechanic prototype in progress.

## Technical details

- **Language:** C++
- **Graphics:** OpenGL (no engine, no framework)
- **Build system:** CMake + vcpkg

## Core mechanic

- Unobserved areas exist in a superposition state
- Pinging the sonar collapses the state — revealing the path but potentially
  spawning walls or hazards
- Players must balance information-gathering against the risk of making the
  level harder

## Building

Requires OpenGL and CMake. Clone the repo and open in Visual Studio or build
via CMake. Dependencies managed via vcpkg (`vcpkg.json` included).

## About

Built by [Austin Espinosa](https://foxglowgames.com) — indie developer and
Information Science student at ICU Tokyo.
