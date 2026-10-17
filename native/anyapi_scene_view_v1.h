#pragma once
#include <stdint.h>
// EXPERIMENTAL (planned for AnyAPI 0.40.0; not yet part of the public SDK): a second camera.
// The game's own gbuffer pass is recorded a second time from this camera into
// AnyAPI's targets, lit with the game's sun, sky and fog colours, and drawn into
// a rectangle of the screen before the HUD.
// mode 1: relative to the player's camera. position: metres along its right, up
//         and forward axes; yaw/pitch (radians) turn from its direction.
// mode 2: absolute. position: world metres; yaw 0 looks along +Z, pi/2 along +X;
//         pitch is up-positive.
// fov_degrees: vertical field of view, 0 keeps the game's. interval: record the
// view every Nth frame (1-8). display: x, y, width, height as fractions of the
// screen; a zero width or height draws nothing. exposure: brightness (0.1-8).
// Only what the game already draws for the player's camera is drawn; objects
// outside the player's view can be missing from other points of view.
struct AnySceneViewParametersV1 {uint32_t struct_size=sizeof(AnySceneViewParametersV1),version=1,enabled=0,mode=1;double position[3]={0,0,0};float yaw=0,pitch=0,fov_degrees=0;uint32_t interval=1;float display[4]={0,0,0,0};float exposure=1;uint32_t reserved=0;};
// error: 0 none; 1-6 camera constants (1 layout, 2 view, 3 product, 4 basis,
// 5 singular, 6 not finite); 10+ native contract; 20+ GPU.
struct AnySceneViewStatusV1 {uint32_t struct_size=sizeof(AnySceneViewStatusV1),version=1,available=0,ready=0,width=0,height=0;uint64_t frames=0,rejected_frames=0;int32_t error=0;uint32_t reserved=0;};
struct AnySceneViewV1 {uint32_t struct_size,version;bool(*set)(const AnySceneViewParametersV1*);bool(*status)(AnySceneViewStatusV1*);};
