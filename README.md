# AC_ReAnswered

ReXGlue-based recompilation workspace for **Armored Core: For Answer** (Xbox 360), prepared for collaborative GitHub use.

This repository contains source and tooling only.  
No copyrighted game files are included.

## Repository Layout

- `assets/` expected game executable input (`default.xex`)
- `generated/` ReXGlue codegen output
- `src/` custom runtime/hooks
- `acfa_recomp_config.toml` ReXGlue project config
- `CMakeLists.txt` / `CMakePresets.json` build config

## Prerequisites

Windows:
- PowerShell 5+ or PowerShell 7+
- CMake, Ninja, LLVM/Clang
- `extract-xiso` (for ISO extraction)
- ReXGlue SDK (`rexglue.exe`)
- Visual Studio Community edition

## How to Build

### Windows

1. Install Rexglue-SDK following the <a href="https://github.com/rexglue/rexglue-sdk/wiki/Guide-%E2%80%90-Getting-Started">wiki</a>
2. install Visual Studio Community edition and ensure you install the desktop development with C++ and make sure you check the box that says C++ clang compiler for windows (note: if you are using Mac or linux you can skip this for you will have to follow the wiki linked in step 1 to build the game)
3. clone/download the repository
4. dump your copy of Armored core for Answer and use a tool like extract-xiso to dump the contents of the iso (INCLUDING THE DEFAULT.XEX FILE)
5. place the contents of the iso in the assets folder(INCLUDING THE DEFAULT.XEX FILE)
6. open the folder in visual studio, go into cmake targets view
7. change the configuration to win-amd64-relwithdebinfo
8. put rexglue.exe in your path environment variable and do ```rexglue codegen acre_config.toml``` in a terminal (visual studios works, or you can use windows default terminal/cmd/powershell)
10. right click ACRE project and select build all
11. copy the assets folder with the dumped contents of the iso in out/build/win-amd64-relwithdebinfo

### Linux

1. Install Rexglue-SDK following the <a href="https://github.com/rexglue/rexglue-sdk/wiki/Guide-%E2%80%90-Getting-Started">wiki</a>
2. clone/download the repository
3. dump your copy of Armored core for Answer and use a tool like extract-xiso to dump the contents of the iso (INCLUDING THE DEFAULT.XEX FILE)
4. place the contents of the iso in the assets folder(INCLUDING THE DEFAULT.XEX FILE)
5. Within this repository, do the following to build:
    - `rexglue migrate --app_root .`
    - `rexglue codegen acre_config.toml`
    - `cmake --preset linux-amd64-relwithdebinfo -D REXSDK_DIR:PATH=/PATH/TO/rexglue-sdk`
    - `cmake --build --preset linux-amd64-relwithdebinfo --target acre_codegen`
    - `cmake --build --preset linux-amd64-relwithdebinfo`
    - `ln -s assets out/build/linux-amd64-relwithdebinfo/assets`
    - `cp configs/acre.toml out/build/linux-amd64-relwithdebinfo/`
6. Run the game: `./out/build/linux-amd64-relwithdebinfo/acre`

