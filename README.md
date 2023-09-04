# Classic Sprinting for Starfield
This mod changes the sprint button to only sprint when the button is held instead of acting like a toggle. It works with keyboards and controllers too.

Available on Nexusmods: https://www.nexusmods.com/starfield/mods/557

Usage:
- Install vcpkg by following the instructions here: https://github.com/microsoft/vcpkg
- Set the environment variable `VCPKG_ROOT` to the directory you installed vcpkg in.
- Clone this repository.

Now you can open the folder in Visual Studio or other IDE of your choice,
or you can run CMake manually (from the root of this repository):
```
cmake -S . -B build
cmake --build build --config Release
```