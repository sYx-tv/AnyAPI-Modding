# Mod sync channel (in development)

Status: **step 1, hello only**. There is no mod-facing service yet. This page records how the channel works so later steps and game updates can check it.

## Why

Mods that need co-op data today have to hide it in values the game already replicates (EngineSound uses a saved `s32` property). The sync channel gives AnyAPI its own message line between players in the same world, so later versions can offer mods a real `send` / `on_message` service.

## How it works

- Anymaker 0.1.25 (Steam 25904830) runs co-op over Steam's `ISteamNetworkingMessages002`, loaded from `steam_api64.dll`. The game reads only small channel numbers. AnyAPI sends and reads on its own far channel, `anyapi_net::CHANNEL` (`0x414E5900`), so the game never sees these messages.
- AnyAPI gets the interface the same way the game does: `SteamInternal_FindOrCreateUserInterface(SteamAPI_GetHSteamUser(), "SteamNetworkingMessages002")`.
- The roster comes from the existing players producer (`native_players.inc`): every non-local peer with a Steam ID.
- AnyAPI **only sends over a Steam session the game already opened** (`GetSessionConnectionInfo` reports connected). It never starts a new connection. A joining client therefore talks to the host, and the host talks to every client. Direct-IP games have no Steam session, so nothing is sent.
- Each peer gets at most 3 hellos, 3 seconds apart. If no answer arrives within 10 seconds of the first one, the log says the peer has no AnyAPI and nothing more is sent to it.
- Incoming messages are dropped unless the Steam-verified sender is in the current roster. A sender gets at most 10 messages per second. Messages are at most 1024 bytes and must parse exactly (magic `ANYN`, protocol 1, no trailing bytes).

Code: `native/anyapi_net_protocol.h` (wire format and handshake state, unit-tested by `native/tests/net_protocol_test.cpp`) and `native/anyapi_net.inc` (the Steam calls). It ticks from the players producer, on the game's main thread.

## Wire format (protocol 1)

| Bytes | Field |
|---|---|
| 4 | magic `0x4E594E41` ("ANYN") |
| 2 | protocol, 1 |
| 1 | kind: 1 hello, 2 hello_ack |
| 1 | reserved, 0 |
| 4 | sender nonce |
| 1 + n | AnyAPI version text |
| 1 | mod count, at most 32 |
| (1 + n) each | mod id text |

Text is 1 to 95 printable ASCII bytes. All integers are little-endian.

## Log lines

```text
NET channel ready transport=SteamNetworkingMessages002
NET hello to <name> (peer N) attempt=1 bytes=.. result=1
NET hello from <name> (peer N) AnyAPI 0.38.1, mods: ...
NET hello_ack from <name> (peer N) AnyAPI 0.38.1, mods: ...
NET <name> (peer N) did not answer, no AnyAPI (or an older one)
NET dropped message reason=not_in_session|rate_limit|bad_format ...
```

`result` is Steam's `EResult` (1 means OK). If any Steam call faults, the channel turns itself off for the rest of the session and logs `NET disabled after a Steam call fault`.

## Testing step 1

It needs two PCs. Host on the laptop and have a friend join.

1. Both players run the test AnyAPI build. Both logs should show `NET hello from` the other player.
2. The friend turns mods off. The host log should show `did not answer`, and the friend's game should behave normally.

## On a game update

Re-check that `game.exe` still imports `steam_api64.dll` and still asks for `SteamNetworkingMessages002`. If the interface version changes, update the string and the vtable order in `anyapi_net.inc`.
