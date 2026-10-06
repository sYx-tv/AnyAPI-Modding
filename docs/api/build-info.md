# Build information

Query `anyapi.build`, version 1, through AnyAPI_GetServices. Include
`anyapi_build_v1.h` and pass an initialized AnyBuildInfoV1 to copy().

The copied status records the loader's exact executable and game.gcl SHA-256
check. The version/build labels identify the supported profile, not a separate
runtime version-string call. Mismatches never enable native hooks. A matching
profile does not mean every optional hook has resolved or passed gameplay testing.

The service has no game pointers and can be read from any thread. ABI 1 is
unchanged; older mods can ignore this optional service.
