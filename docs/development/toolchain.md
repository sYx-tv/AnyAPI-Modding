# Tools used to build these DLLs

Mods and the API host are written in C++20. Microsoft's MSVC compiler/linker from
Visual Studio 18 Community produces Windows x64 DLLs (installed toolset 14.51.36231).
CMake configures the project; MSBuild compiles it; CTest runs the regression checks.
Python scripts inspect installed assets, generate the road graph, verify contracts and
assemble ZIP files. Capstone is used for reviewed x64 disassembly. dumpbin inspects DLL
imports. Windows GDI+ paints the map/item browser; AnyAPI composites those canvases
through Direct3D 12, Direct3D 11-on-12 and Direct2D. These are libraries, not separate apps.

Friends do not need Visual Studio, Python or CMake to play with the runtime ZIP.
The DLLs import Microsoft's x64 Visual C++ runtime (MSVCP140/VCRUNTIME140/VCRUNTIME140_1)
and Windows graphics libraries. If those runtime DLLs are missing, install Microsoft's
current x64 Visual C++ Redistributable. No compiler is required on their machine.

Source layout: `native/` holds implementations and tests, `sdk/include/` holds
public headers, and `docs/` holds API contracts and mod guides. The local road cache
is `generated/roads.bin`. See [Building](building.md) for complete commands.
