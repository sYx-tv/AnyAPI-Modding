# MCCode

> MCCode hooks game functions directly through the [experimental SDK](../../sdk/experimental/README.md),
> so it only activates on Anymaker 0.1.28 / Steam build 26277967 and turns itself off on any other build.
> [Release notes](../releases/mccode-0.2.0.md). Needs AnyAPI 0.38.0 or newer.

MCCode adds a **Code view** to the microcontroller editor, so you can type the program instead of dragging
blocks. Open any microcontroller and press **`</> Code`** in the top-right corner.

The code turns into normal blocks when you press **Apply** (or Ctrl+Enter). Saving, the game's Undo, block
editing and multiplayer keep working the usual way, and you can switch between code and blocks at any time.

## How it works

The game already stores microcontroller programs as text. The Save/Load tab writes and reads plain `.txt`
files in the game's `scripts` folder, and the server has a parser that builds blocks from that text.

- Opening the Code view runs the game's own Save, catches the text before it is written to disk, and shows it
  as friendly code.
- Apply translates your code into the game's text and sends it with the game's own Load. The server rebuilds
  the blocks from it. It is one Undo step.
- MCCode checks your code before anything is sent. A line with a mistake gets a red underline and a message
  in the problems strip, and nothing reaches the game until it is fixed, so a typo can't wipe the program.

## The language

The game's language, with a few shortcuts. Anything the game itself writes is also valid here.

```
define in fractional engine_rpm
define in bool launch_switch
define out integer two_step
define var integer count

# launch control
on_tick {
	if launch_switch and engine_rpm < 100.0 {
		two_step = 4000
	} else {
		two_step = 0
	}
	count = (count + 1) % 60
}
```

| What | Write | Notes |
| --- | --- | --- |
| Blocks | `on_tick { }` | Runs every game tick. The game has an `on_render` word but never runs it and won't load it, so MCCode rejects it |
| Variables | `define var integer speed` | Types: `bool`, `integer`, `fractional`, `string`, `analogue`, or a `define type` |
| Lists | `define list fractional samples` | `samples.append(x)`, `samples.insert(i, x)`, `samples.erase(i)`, `samples.clear()`, `samples[i]`, `len(samples)` |
| Parts | `RearLeft.rps`, `RearLeft.brake_factor = 1.0` | A part wired to the microcontroller needs no define: read or set its channels with a dot, using the names the game shows |
| Ports | `define in fractional rpm`, `define out bool lamp` | Your own inputs and outputs, read and written by name |
| Setting | `speed = speed + 1` | |
| Tests | `if x { } elseif y { } else { }` | `else if` works too |
| Loops | `repeat 4 { }`, `foreach samples { }` | |
| Functions | `define function arm(integer limit) { }`, then `arm(4000)` | Parameters are local to the function |
| Types | `define type Wheel { define member fractional grip }` | Then `define var Wheel front` and `front.grip` |
| Maths | `+ - * / % ^`, `abs() floor() ceil() round() sqrt() sin() cos() tan() asin() acos() atan() exp() ln() log2()` | Normal precedence: `a + b * c` multiplies first. `^` is power |
| Conversions | `to_integer(x)`, `to_fractional(x)` | The game keeps integers and fractionals apart |
| Compare | `== != > < >= <=`, `contains` | |
| Logic | `and or not` (or `&& \|\| !`) | |
| Values | `true`, `false`, `12`, `1.5`, `"text"`, `rand(low, high)` | Fractionals need a dot: `100.0` |
| Comments | `# note` | A comment at the end of a line moves onto its own line |

**Suggestions while you type.** A list pops up under the word you are typing with the names that fit:
your variables, inputs and outputs, functions, keywords, and the parts wired to the microcontroller. Type a
part name and a dot (`RearLeft.`) to list its channels. Up/Down picks, Tab or Enter fills it in, Esc closes.
Brackets and quotes close themselves: typing `(` gives `()` with the cursor inside, typing `)` steps over
the closer, and `{` then Enter opens an indented block with its `}` below.

**Names.** Write names on their own. If the same name is both a variable and a port, add what it is:
`var x`, `in x`, `out x` (or `param x` inside a function).

**What it sends.** The game's text puts brackets around every operation and the kind in front of every name.
For example, `count = (count + 1) % 60` is sent as `var count = ((var count + 1) % 60)`, and `x != 3` becomes
`! ((var x == 3))`, because the game has no "not equal". Negative numbers become `(0 - 5)`.

## Nodes

Press **Nodes** in the toolbar to build the program from boxes and wires instead, like Stormworks
microcontrollers. Press **Code** to go back to the text; both show the same program.

- **Add node** (side panel) lists every node by group. Click one to drop it in the middle of the canvas.
- Drag from an output (right side of a node) to an input (left side) to wire them. Blue ports carry numbers,
  green ports carry on/off. An input takes one wire; dragging a wired input picks its wire up again, and
  right-clicking an input unplugs it.
- Click a node to select it, drag it to move it, double-click it (or press Enter) for **Selected**, where you
  set its part name, channel and values. Delete removes it, Ctrl+D duplicates it.
- Drag empty space to pan, use the wheel to zoom, Home to go back to the start.
- **Apply** turns the graph into the game's blocks like typed code does. While the applied program runs, each
  output shows its live value in yellow.
- Nodes with a problem (a missing part name, or an error the game reports on their blocks) get a red outline
  and a message under them. Click the message in the problems strip to jump to the node.

| Group | Nodes |
| --- | --- |
| Parts | Part input, Part output (number or on/off), Number, On/off |
| Maths | Add, Subtract, Multiply, Divide, Remainder, Power, Min, Max, Clamp, Absolute, Negate, Square root, Sine, Cosine |
| Compare | Greater than, Less than, At least, At most, Equal, Not equal, Threshold |
| Logic | AND, OR, NOT, XOR, Number switch |
| Memory | SR latch, Push to toggle, Pulse, Memory, Counter, Change |
| Timing | Timer, Blinker |
| Control | Ramp, Smooth, PID |

A Part input reads a wired part's channel, for example part `RearLeft` channel `rps`. A Part output sets one,
for example `Actuator_Stepper_Motor` channel `input`. Every tick the nodes run from inputs to outputs; a wire
that loops back reads the value from the tick before.

**How it is saved.** The graph is stored in the program itself as comment lines starting with `#@`, so a
node program reopens as nodes, on any computer with the mod. Players without the mod see ordinary blocks. If
you change node-made code by hand in the Code view, the nodes no longer match it: MCCode warns you, and editing
the nodes again replaces the hand-made changes (Ctrl+Z in the Code view brings them back).

## The side panel

- **Commands**: ready-made scripts at the top (traction control, ABS, cruise control, launch control, shift
  light, blinker), then every keyword and function with a one-line explanation. Click a command to insert it at
  the cursor. Click a script to fill an empty program with it, or add it at the end of yours. The scripts use
  example part names like `RearLeft` and `Throttle.input`; rename them to the parts This MC lists, then Apply.
- **This MC**: this microcontroller's inputs, outputs, variables, lists, functions and types, read from your code
  as you type, plus the ports the game reports. Click a name to insert it.
- **Watch**: live variable and port values from the game's debugger, while the program runs or is paused.
- **Parts**: a guide to setting up a microcontroller. It covers wiring and naming parts, reading and setting a
  channel, and on_tick, with examples you can click to insert. Below the guide are the channels of the parts wired
  to this microcontroller, then every part in the game that has channels, grouped by category. Click a part to see
  each channel's type and the game's own description of it, and click a channel to insert `Part_Name.channel`.
  The list is read at run time from your game's part definitions through AnyAPI's item catalog, plus the part
  definition files mods write under `AnyAPI and Modding` (GPS Sensor, turbos, the Dyno and so on, marked with the
  mod's folder name). It is built at startup and rebuilt when a world loads new parts or you open the tab, so it
  follows game updates and new mods. Each channel says whether code reads it (the part's output) or sets it (its input). Parts that aren't listed, like gas and liquid
  valves, have no channels: wire their input to a part that has a channel and an output, such as the stepper motor.

## Debugging

The buttons drive the game's own microcontroller debugger.

| Action | Button | Key |
| --- | --- | --- |
| Breakpoint on a line | Click left of the line number | F9 |
| Pause / resume | Pause / Resume | F5 |
| Step into / over / out | Step in / Step over / Step out | F11 / F10 / Shift+F11 |

While paused, the running line is highlighted and kept on screen. Errors the game finds (for example
"Variable used before it is defined") show on their line in orange.

Breakpoints and the running line need the code to match the blocks. After you edit, press Apply again first.
MCCode lines up your code with the game's blocks by counting statements; if a script ever doesn't line up,
the log says so and only these line features turn off.

## Keys

| Key | Action |
| --- | --- |
| Ctrl+Enter or Ctrl+S | Apply |
| Esc | Back to the blocks |
| Ctrl+Z / Ctrl+Y | Undo / redo typing (the toolbar Undo is the game's block Undo) |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / cut / paste (Windows clipboard) |
| Ctrl+A | Select all |
| Ctrl+D | Duplicate the line |
| Tab / Shift+Tab | Indent / unindent (the selected lines) |
| Ctrl+arrows | Move by word; Ctrl+Up/Down scrolls |
| Double-click | Select a word |
| Mouse wheel | Scroll (Shift+wheel scrolls sideways) |

In the Nodes view: Delete removes the selected node, Ctrl+D duplicates it, Ctrl+Z / Ctrl+Y undo and redo node
changes, Enter opens its settings, Home recentres, Esc goes back to the blocks.

**Copy all** copies the whole program, handy for sharing or keeping a backup in a text file.

## Limits

- If a program uses something MCCode can't translate, the Code view shows the game's own text unchanged. You
  can still edit it and Apply sends it as written.
- Apply replaces the whole program, so the game's block Undo goes back to the previous program in one step.
- Every player who wants the Code view needs the mod. The program itself is ordinary blocks, so players without
  the mod see and run it normally.

## Log

MCCode writes to `anymaker_modding.log` beside `game.exe`: `Ready after N s` once it found the game functions,
then a line for each program it reads or loads. `PARTS_GUIDE parts_with_channels=N channels=N mod_parts=N
mod_files=N (why)` counts the parts the Parts tab found each time it builds its list. `line map off` means breakpoints and the running line were
turned off for that script.
