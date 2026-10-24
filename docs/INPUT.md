# Keyboard, controller and mouse input

Bindings live in `input.ini`, next to `bbport.ini` (same folder rule: `BB_INPUT_CONFIG` if set,
otherwise the directory of `BB_CONFIG`/`bbport.ini`). The Mac runtime uses this same parser.
If the file does not exist, the runtime creates
it with the defaults below the first time the pad is opened.

The syntax is shadPS4's input config format (`output = input`, one input per line), so **a
Bloodborne config made for shadPS4** (`user/input_config/CUSA03173.ini` or `default.ini`) can be
copied over `input.ini` as is. Lines shadPS4 supports that bbport does not yet (`key_toggle`,
hotkeys other than the two below, `override_controller_color`, mouse-as-gyro, mouse-as-touchpad)
are skipped with one log warning per line; the rest of the file still loads.

Keyboard and controller bindings are always combined, not a fallback: the keyboard works
whether or not a controller is connected. When more than one source is bound to the same
output, buttons need only one source held; stick axes add their contributions together (and
are then clamped and passed through the deadzone), matching shadPS4.

## Outputs

| Output | What it does |
|---|---|
| `cross` `circle` `square` `triangle` | Face buttons |
| `l1` `r1` `l2` `r2` `l3` `r3` | Shoulders, triggers, stick clicks |
| `options` | Pause/menu |
| `pad_up` `pad_down` `pad_left` `pad_right` | D-pad |
| `touchpad_left` `touchpad_center` `touchpad_right` | A touch at that position on the pad |
| `axis_left_x_minus`/`_plus`, `axis_left_y_minus`/`_plus` | Left stick, one direction each |
| `axis_right_x_minus`/`_plus`, `axis_right_y_minus`/`_plus` | Right stick, one direction each |
| `axis_left_x` `axis_left_y` `axis_right_x` `axis_right_y` | A stick bound to a full physical axis (e.g. another stick) |
| `leftjoystick_halfmode` `rightjoystick_halfmode` | Holding this input halves that stick's deflection (walk) |
| `hotkey_toggle_mouse_to_joystick` | Key that toggles mouse look on/off (default `f7`) |
| `hotkey_reload_inputs` | Key that reloads this file without restarting (default `f8`) |

`l2`/`r2` also carry an analog value: a trigger bound to them passes its own reading; a key or
button snaps it to the maximum, same as the previous fixed layout.

## Inputs

- **Keyboard:** letters/digits (`a`..`z`, `0`..`9`), `f1`..`f12`, `space`, `enter`, `tab`,
  `backspace`, `escape`, `lshift`/`rshift`, `lctrl`/`rctrl`, `lalt`/`ralt`, arrow keys
  (`up`/`down`/`left`/`right`), the numpad (`kp0`..`kp9`, `kpslash`, ...) and the usual
  punctuation keys (`comma`, `period`, `semicolon`, ...). Names follow what the key shows on the
  active keyboard layout, like shadPS4 — not its physical position.
- **Mouse buttons:** `leftbutton`, `middlebutton`, `rightbutton`, `sidebuttonback`,
  `sidebuttonforward`.
- **Mouse wheel:** `mousewheelup`, `mousewheeldown`, `mousewheelleft`, `mousewheelright`.
- **Controller buttons:** `cross`, `circle`, `square`, `triangle` (by position, not the printed
  letter), `l1`, `r1`, `l3`, `r3`, `options`, `pad_up`/`pad_down`/`pad_left`/`pad_right`,
  `back`/`share`, paddles (`lpaddle_high`, `l4`, `l5`, ...).
- **Controller axes:** `l2`, `r2` (triggers, 0..255), `axis_left_x`/`axis_left_y`/`axis_right_x`/
  `axis_right_y` (a full stick axis) and their half-axis forms (`axis_left_x_minus`,
  `axis_left_x_plus`, ...).
- **`unmapped`:** an explicit "no binding", different from leaving the line out.

Up to 4 bindings per output; a 5th is accepted and logged as ignored.

## Combinations

```ini
r1 = leftbutton
r2 = lshift, leftbutton          # Shift + left click: strong attack
l2 = lctrl, lshift, rightbutton  # up to three inputs
```

Inputs separated by commas must all be held together, as in shadPS4. They can be keys, mouse
buttons, wheel steps and controller buttons, each at most once; analog sticks and triggers
cannot be part of a combination. A held combination takes priority over the bindings on its parts:
with the lines above, Shift + left click is R2 only, not R1 as well, and a binding on Shift alone
is released while the combination is held. Three inputs beat two the same way.

Bloodborne dodges when Circle is *released*, and running is Circle held. So a key that is an
action of its own and also part of a combination is pressed on the way into it: with
`circle = lshift` (the default), Shift + left click starts a sprint and then lets go of Circle,
which the game reads as a dodge. Give a modifier no action of its own, or use another modifier. Edit the file and
press F8 to reload. The removed desktop launcher's layout presets and automatic
button-prompt replacement are not implemented in the Mac renderer.

## Mouse look on macOS

```ini
mouse_to_joystick = right
mouse_movement_params = 0.5, 1, 0.125
```

F7 toggles capture. Mouse motion drives the right stick, retaining the game's
turn-speed limit and acceleration. Opening Mac settings or character-name entry
releases capture. Click the game window to capture again; that click is consumed.

## Inverting and vertical sensitivity

```ini
mouse_invert_x = off        # on | off
mouse_invert_y = off        # on | off
mouse_sensitivity_y = 1.0   # 0.1..10: up/down as a multiple of left/right
```

These scale the mouse motion itself, so they apply to both camera modes (stick and direct) and
to the stick the mouse drives while locked on.

`mouse_camera = direct` is accepted by the shared config parser for compatibility,
but the Mac input adapter currently falls back to mouse-to-stick mode. The old
Windows camera hook has been removed; direct 1:1 camera movement is unavailable.

## Keyboard movement: opposite keys and quick direction changes

```ini
stick_socd = last              # last (default) | neutral
stick_release_delay_ms = 60    # 0..200, left stick only; default 0 (off)
```

A neutral stick ends a sprint, and two things used to produce one when strafing A <-> D:

- **Both keys down during the switch.** With `stick_socd = last`, two opposite directions held
  together give the one pressed most recently (keyboard "snap tap"), instead of cancelling out.
  `neutral` restores the old sum-and-cancel behavior (shadPS4's). Applies to both sticks' half-axis
  bindings (keys, buttons, half sticks); full-axis bindings are unaffected.
- **A gap between releasing one key and pressing the next.** `stick_release_delay_ms` keeps the
  left stick's last direction for that long after its keys are released; any new direction
  replaces it immediately. 40-80 ms covers a fast key switch while still stopping on the spot
  when you let go.

```ini
stick_circular = on            # on (default) | off
stick_turn_rate = 900          # degrees per second, 0..5000; default 0 (instant)
```

Keys alone drive the left stick like no real thumbstick can:

- **Square diagonals.** W+A is (-127,-127), about 1.41x the reach of a real (round) stick.
  `stick_circular = on` clamps it to the circle: a diagonal is full deflection, like a controller.
- **Direction jumps.** Going from W to W+A turns the stick 45 degrees within one sample.
  `stick_turn_rate` rotates it toward the new direction at that many degrees per second instead,
  at full deflection the whole way (so a sprint is kept), like a thumb sliding along the gate.
  900 turns 45 degrees in 50 ms. A reversal of more than 150 degrees (A <-> D, W <-> S) still
  snaps, as a flick would, and after a release the next key starts straight at its direction.

## Analog deadzone

```ini
analog_deadzone = leftjoystick, 1, 127   # device, inner, outer (1..127 each)
```

`device` is `leftjoystick`, `rightjoystick`, `l2` or `r2`. Below `inner` the output is 0; from
`inner` to `outer` it ramps up linearly; at or above `outer` it is fully deflected. The default
(`1, 127`) is effectively no deadzone, matching the behavior before this feature existed.

## Reserved keys

**F1**, **Insert** (the settings menu) and **F9** (`BB_PAD_RECORD`) can never be bound to a game output,
and are rejected with a warning if a line tries. **Escape** can be bound (e.g. `options = escape`):
the settings menu only uses it to close itself, and the game gets no input while that menu or the
text entry box is open. Whatever key currently toggles
mouse look or reloads the file (`f7`/`f8` by default, or whatever `hotkey_toggle_mouse_to_joystick`/
`hotkey_reload_inputs` were last set to) is reserved the same way.

## Default file

```ini
# bbport input (shadPS4 syntax: output = input, one input per line).
# A shadPS4 input config (user/input_config/CUSA03173.ini) can be copied over this file.
# F7 toggles the mouse, F8 reloads this file.

# Keyboard
cross = space
circle = lshift
square = e
triangle = v
l1 = 1
r1 = 3
l2 = r
r2 = f
l3 = z
r3 = q
r3 = c
options = enter
pad_up = i
pad_down = k
pad_left = j
pad_right = l
touchpad_left = tab
touchpad_right = backspace

axis_left_x_minus = a
axis_left_x_plus = d
axis_left_y_minus = w
axis_left_y_plus = s
axis_right_x_minus = left
axis_right_x_plus = right
axis_right_y_minus = up
axis_right_y_plus = down

# Hold to halve the left stick (walk): uncomment and pick a key
# leftjoystick_halfmode = lalt

# Mouse (uncomment mouse_to_joystick to turn it on; buttons work only while captured)
# mouse_to_joystick = right
mouse_movement_params = 0.5, 1, 0.125
r1 = leftbutton
r2 = rightbutton
circle = sidebuttonback
square = sidebuttonforward

# Controller
cross = cross
circle = circle
square = square
triangle = triangle
l1 = l1
r1 = r1
l2 = l2
r2 = r2
l3 = l3
r3 = r3
options = options
touchpad_left = back
pad_up = pad_up
pad_down = pad_down
pad_left = pad_left
pad_right = pad_right
axis_left_x = axis_left_x
axis_left_y = axis_left_y
axis_right_x = axis_right_x
axis_right_y = axis_right_y

# Hotkeys
hotkey_toggle_mouse_to_joystick = f7
hotkey_reload_inputs = f8
```

Without an `input.ini`, this default reproduces the previous fixed layout, except: the keyboard
now works together with a controller instead of only without one, and **Q is lock-on (R3)**
instead of Triangle — **V is Triangle** now, and **C still works as R3** too. The mouse starts
off.

## Changing bindings while the game runs

Edit `input.ini` and press **F8** (or whatever `hotkey_reload_inputs` is set to). The whole file
is re-read and swapped in for the next pad sample — no restart needed. If the file cannot be
read (e.g. a typo left it without a closing line, or it was deleted mid-edit), the previous
bindings keep working and a warning is logged; the game is never left without any input config.

## See also

- [specs/keyboard-and-mouse/spec-design-keyboard-mouse-input.md](../specs/keyboard-and-mouse/spec-design-keyboard-mouse-input.md) —
  full design spec, requirement by requirement.
- [docs/MODS.md](MODS.md) — loose-file mods and third-party game patches.
