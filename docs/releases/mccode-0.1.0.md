# MCCode 0.1.0

**New mod: type your microcontroller code.** Open any microcontroller and press **`</> Code`** in the top right
for a Code view beside the blocks. Write the program as text and press Apply (or Ctrl+Enter): the game builds the
blocks from it, so saving, multiplayer and players without the mod all work as usual. Switch back to the blocks
at any time.

- **Friendly code:** the game's own text language with shortcuts: normal maths precedence, `and`/`or`/`not`,
  `!=`, `else if`, plain names without `var`/`in`/`out` in front, and wired parts by name (`RearLeft.rps`). Anything
  the game writes is valid too.
- **Checked before it's sent:** a mistake gets a red underline and a message, and nothing reaches the game until
  it's fixed, so a typo can't wipe the program.
- **Side panel:** every command with a one-line explanation (click to insert), this microcontroller's ports,
  variables and functions, and live values while it runs.
- **Debugger:** breakpoints (F9), pause and resume (F5) and step in, over and out (F11, F10, Shift+F11) drive the
  game's own debugger, with the running line highlighted in your code.
- **Editor keys:** undo/redo, copy/cut/paste with the Windows clipboard, Tab to indent, Ctrl+D to duplicate a line,
  and **Copy all** to share or back up a program.

Anymaker 0.1.25 only; needs AnyAPI 0.38.0 or newer. See [MCCode](../mods/mccode.md).
