# AC_ReAnswered

ReXGlue-based recompilation workspace for **Armored Core: For Answer** (Xbox 360), prepared for collaborative GitHub use.

This repository contains source and tooling only.  
No copyrighted game files are included.

## Repository Layout

- `assets/` local game files input directory, kept empty in Git except for a placeholder `.gitignore`
- `generated/` local ReXGlue codegen output directory, kept empty in Git except for a placeholder `.gitignore`
- `src/` custom runtime/hooks
- `ACRE_config.toml` ReXGlue project config
- `CMakeLists.txt` / `CMakePresets.json` build config

## Prerequisites

Windows:
- PowerShell 5+ or PowerShell 7+
- CMake, Ninja, LLVM/Clang
- ReXGlue SDK (`rexglue.exe`)
- Visual Studio Community edition

## How To Build

1. Install the ReXGlue SDK by following the official getting started guide:
   https://github.com/rexglue/rexglue-sdk/wiki/Guide-%E2%80%90-Getting-Started
2. Install Visual Studio Community with Desktop development with C++ and the Clang tools for Windows workload component.
3. Clone this repository.
4. Dump your legally owned Armored Core: For Answer game files and place the extracted contents, including `default.xex`, into `assets/`.
5. Ensure `rexglue.exe` is available on your `PATH`.
6. Run code generation:

```powershell
rexglue codegen .\ACRE_config.toml
```

7. Configure and build:

```powershell
cmake --preset win-amd64-relwithdebinfo
cmake --build --preset win-amd64-relwithdebinfo --parallel
```

8. The build output will be written under `out/build/win-amd64-relwithdebinfo/`.

## Notes

- `assets/` and `generated/` are intentionally committed as empty directories with placeholder `.gitignore` files.
- `out/` is local build output and is intentionally not tracked by Git.
