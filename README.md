# AC_ReAnswered

ReXGlue-based recompilation workspace for **Armored Core: For Answer** (Xbox 360), prepared for collaborative GitHub use.

This repository contains source and tooling only.  
No copyrighted game files are included.

## Repository Layout

- `assets/` local game files and extracted data, ignored by Git
- `generated/` local ReXGlue codegen output, ignored by Git
- `src/` custom runtime/hooks
- `ACRE_config.toml` ReXGlue project config
- `CMakeLists.txt` / `CMakePresets.json` build config

## Prerequisites

Windows:
- PowerShell 5+ or PowerShell 7+
- CMake, Ninja, LLVM/Clang
- ReXGlue SDK / `rexglue.exe`
- Local Armored Core: For Answer game files in `assets/`

Linux:
- CMake, Ninja, Clang
- ReXGlue SDK
- Local Armored Core: For Answer game files in `assets/`

## Manual Build

Windows:
```powershell
rexglue.exe codegen .\ACRE_config.toml
cmake --preset win-amd64-release
cmake --build --preset win-amd64-release --parallel
```

Linux:
```bash
/path/to/rexglue codegen ./ACRE_config.toml
cmake --preset linux-amd64-release -DREXSDK_DIR=/path/to/rexglue-sdk
cmake --build --preset linux-amd64-release --parallel
```

## Notes

- Hook/function addresses are binary-revision specific.
- Keep durable edits in `ACRE_config.toml` and `src/*.cpp`, not `generated/*.cpp`.
- `assets/`, `generated/`, and `out/` are intentionally excluded from Git so collaborators can regenerate them locally.
