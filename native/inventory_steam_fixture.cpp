// Test-only Steam surface. Never install this DLL in the game directory.
static int ready;
extern "C" __declspec(dllexport) int SteamAPI_GetHSteamUser(){return ready;}
extern "C" __declspec(dllexport) void* SteamAPI_SteamApps_v008(){return ready?&ready:nullptr;}
extern "C" __declspec(dllexport) const char* SteamAPI_ISteamApps_GetCurrentGameLanguage(void* apps){return apps==&ready?"russian":nullptr;}
extern "C" __declspec(dllexport) void FixtureSteamReady(int value){ready=value;}
