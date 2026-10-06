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

Build source layout: Phase34 contains code/public headers/tests. CMake expects
build-phase34/roads_revision2.bin next to Phase34. Generate that reviewed road graph
from a compatible installed game using generate_road_graph.py; game assets are not bundled.
Configure CMake for Visual Studio x64, set ANYAPI_GAME_DIRECTORY to the installed game
folder, build Release, and run CTest in Release. See SOURCE_BUILD.md in the source ZIP.
