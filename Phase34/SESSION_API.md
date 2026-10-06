# Session v1

Query anyapi.session version 1 and validate AnySessionV1. copy(AnySessionStateV1*) returns a copied recent native mode, normalized AnySessionMode and the native mode name. Invalid, unknown or older than 500 ms samples return false. Treat false/UNKNOWN as no permission; never use a remembered allowed mode.

The current game descriptor supplies enum names at runtime; numeric mode values are not guessed. Exact native bodies and dependency cells are pinned in INVENTORY_ACTION_CONTRACTS.json. The existing guarded local-player callback verifies the native mode getter and reads the replicated property before publishing a copy. Startup logs SESSION_MODE ready=1 enum_names=..., then changes log SESSION_MODE mode=... copied=1. No per-frame mode logging.

AnyInventory chooses Sandbox/Creative through normalized names; the generic service also recognizes Survival/Career and leaves unrecognized names UNKNOWN. No joining-client acceptance is asserted. See examples/session_mode.cpp and inventory_request_masked.cpp.

Live Steam startup on 2026-10-05 independently verified the native descriptor as 0=survival, 1=creative, 2=sandbox. Runtime still resolves names rather than assuming those indices. See NATIVE_GAMEMODE_NAMES_20261005.json. Career is not in this build’s descriptor; an unrecognized mode never grants permission.
