# Item images v1

Query anyapi.item_images version 1 and validate AnyItemImagesV1 size/version. copy(catalog_index, bytes, capacity, required) returns a copied 128×128 transparent BGRA image, pitch 512, 65536 bytes. required is mandatory; insufficient capacity leaves the buffer unchanged. Invalid indices return false. An image failure does not remove catalog entries. Images use immutable catalog indices from the current process.

The host lazily parses installed v5 item mesh assets, validates part counts, block sizes, 28/36-byte vertex layouts, finite geometry and triangle indices, then renders an orthographic preview with vertex colours and depth testing. This does not create game items or invoke native graphics from another thread. Results are cached for the process lifetime. UV textures, shaders and colour-pattern variants are not reproduced. See item_mesh_preview.h, anyapi_item_catalog.inc and item_preview_test.cpp. All 372 current item meshes render successfully.

Revision 14 supports all 970 catalog entries, using inventory mesh_file or component mesh_static.mesh_path and meshes_dynamic[].path. Static/dynamic meshes combine at authored origins; animation poses and preview rotations are not applied. Position/colour-only v5 vertices are also supported. The complete catalog is rendered in the asset regression test; previews remain approximate software images without native materials.

The parser bounds the optional texture-name field before reading geometry; texture bytes are not sampled by the software renderer.
