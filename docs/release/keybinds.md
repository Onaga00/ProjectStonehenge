# Controls

You can play with a keyboard and mouse, with a gamepad, or with both at once.

## Keyboard and mouse

The layout follows ACE COMBAT 7 on the PC.

### Flying

| What it does | Key | Mouse |
|---|---|---|
| Nose up / nose down | 3 / 1 | move the mouse back / forward |
| Roll left / right | A / D | move the mouse left / right |
| Yaw (rudder) left / right | Q / E | |
| Speed up / slow down | W / S | |
| Machine gun | Left Ctrl | left button |
| Missile | Space | right button |
| Change weapon | 2 | wheel up or down |
| Change target | R | middle button (wheel click) |
| Map (each press: radar, close map, half map, whole map) | M | |
| Change view | V | |
| Look behind | C | |
| Level the aircraft | X | |
| Look around | Keypad 8 / 2 / 4 / 6 | hold **Alt** and move the mouse |
| Pause | Esc | |

- The mouse steers only while you are flying. In menus and while paused it works as a normal pointer.
- The mouse side buttons (back and forward) do nothing until you give them a job (see the end of this page).

### Menus

| What it does | Key |
|---|---|
| Move | arrow keys |
| Confirm | Enter |
| Back | Backspace |

## Gamepad

The gamepad works like the original PlayStation 2 controller. Its buttons are matched by position:
the bottom face button is Cross, the right one Circle, the left one Square and the top one Triangle
(on an Xbox pad: A, B, X, Y).

What each button does in a mission is chosen in the game itself: **OPTIONS > CONTROLLER SETUP >
CONTROL TYPE**. That page also shows the full list for the type you pick.

## Changing the controls

All controls are kept in one text file:

`%LOCALAPPDATA%\ProjectStonehenge\controls.ini`

To open it, press **Windows + R**, paste the line above and press Enter. The file opens in Notepad
(or choose Notepad if Windows asks).

1. Find the line of the control you want to change, for example `Missile = Space, Mouse Right`.
2. Write the new key after the `=`. To use more than one key, separate them with commas:
   `Missile = F, Mouse Back`. To leave a control with no key, write nothing after the `=`.
3. Save the file. The change works at once, even while you play.

Good to know:

- The top of the file lists every key, mouse button and gamepad button name you can use.
  The mouse side buttons are called `Mouse Back` and `Mouse Forward`.
- In the `[Mouse]` part you can change what the mouse steers, how fast it reacts
  (`Sensitivity`, from 0.1 to 10; the default is 1.0), invert up and down, and which key swaps it to
  looking around (`Hold to swap stick`).
- If you mistype a name, only that line goes back to its default; the rest of your changes stay.
- To get all the default controls back, delete the file. It is written again the next time you
  start the game.
