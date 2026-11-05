# Split-screen: local players and their controllers

Titles built with ReXGlue can sign in up to four local players, as a console does when a second person picks up a controller. This page explains how that works, which settings control it, and how to give each player their keyboard, mouse or gamepad. The settings are cvars in the title's settings file, for example `settings/hardware.toml` (`key = value`, read at startup).

Split-screen itself is the game's feature. The runtime only tells the game which players are signed in and which controller belongs to whom. A game without local multiplayer ignores the extra players. A game with it (split-screen, local co-op, "press START to join") sees them as it would on the console.

By default nothing changes: one player is signed in, and gamepad N drives guest user N.

## Settings

| Setting | Default | What it does |
|---|---|---|
| `max_local_users` | `1` | How many local players can be signed in, 1 to 4. `1` signs in the first player only (single-player behaviour). With 2 to 4, players past the first are signed in while a controller is assigned to them. |
| `user_gamertag_2` | `""` | Name the game shows for the second player. Empty: the first player's name with `(2)` after it. At most 15 bytes. |
| `input_player1` | `auto` | Devices of player 1, comma-separated (see [Choosing devices](#choosing-devices-input_player1-input_player2)). |
| `input_player2` | `auto` | Devices of player 2, in the same form. |
| `keyboard_own_player` | `false` | In `auto` mode: keyboard and mouse are player 1 on their own, and gamepads start at player 2. Lets two people play with one gamepad. Ignored once `input_player1` or `input_player2` is set. |

Two settings are needed for a second player: `max_local_users` to let them sign in, and a controller assigned to them. The other settings only choose which controller that is.

## How players sign in

- Player 1 is always signed in.
- Players 2 to `max_local_users` are signed in while a controller is assigned to them, and signed out when it goes away. In practice, connecting a gamepad signs a player in and disconnecting it signs them out, like a guest signing in with their own pad.
- The runtime checks the assignments at most every 250 ms. When the set of signed-in players changes, the game gets the system notification `XN_SYS_SIGNINCHANGED` (the one a console sends), and the log shows `XAM: local users signed in: 1 -> 3` (a bit mask: bit 0 is player 1, bit 1 is player 2, and so on).
- A player past `max_local_users` is never signed in, even with a gamepad assigned. That gamepad's input still reaches the guest user, so a game that reads controllers without asking who is signed in still sees it.

What a signed-in player gets, as far as the game can tell:

| | Player 1 | Players 2-4 |
|---|---|---|
| XUID | `B13EBABEBABEBABE` | `B13EBABEBABE0001` to `B13EBABEBABE0003` |
| Name shown by the game | the profile name (`User`) | `User(2)`, `User(3)`, `User(4)`; player 2 uses `user_gamertag_2` when set |
| Title profile settings (controls, sensitivity, options the game stores in the profile) | `<content root>/<title id>/profile/User/` | `<content root>/<title id>/profile/User2/` to `User4/` |
| Saves and other content (XContent) | under player 1's XUID | also under player 1's XUID: saves are shared |

So each player keeps their own in-game options (inverted look, sensitivity, button layout) between runs, while save games stay in one place. A game that reads one player's profile through another player's XUID gets that player's settings, as on the console.

Sign-in, names, XUIDs, privileges, membership tier, content restrictions and profile settings all answer per player. Calls about a player who isn't signed in fail the way they do on a console ("no such user" or "not signed in").

## Assigning devices

Every input device has a guest user it feeds:

- **Gamepads** are numbered in the order they were connected: `pad1` is the first one connected, `pad2` the second, up to `pad4`. A pad keeps its number while it stays connected: unplugging pad 1 does not turn pad 2 into pad 1.
- **Keyboard and mouse** are one device (the keyboard and mouse driver, which emulates a controller).
- The runtime also has an internal stand-in device that keeps player 1 connected while no real device is. It always belongs to player 1.

### Automatic mode (default)

With `input_player1` and `input_player2` both `auto`:

| `keyboard_own_player` | Player 1 | Player 2 | Player 3 | Player 4 |
|---|---|---|---|---|
| `false` (default) | keyboard and mouse + pad 1 | pad 2 | pad 3 | pad 4 |
| `true` | keyboard and mouse | pad 1 | pad 2 | pad 3 |

Automatic mode is the only way to get players 3 and 4.

### Choosing devices (`input_player1`, `input_player2`)

Setting either one switches to chosen devices for players 1 and 2. Each value is a comma-separated list of:

| Item | Meaning |
|---|---|
| `keyboard` | keyboard and mouse |
| `pad1` to `pad4` | that gamepad, by connection order |
| `pads` | every gamepad the other player does not name |
| `none` | nothing (an empty list does the same) |
| `auto` | for player 1: keyboard and every gamepad left over (`keyboard,pads`); for player 2: nothing |

Rules:

- A gamepad named by both players belongs to player 1. So does the keyboard.
- A gamepad nobody names, with no `pads` to take it, drives nobody.
- Players 3 and 4 get no devices in this mode.
- Spaces around the items are ignored; unknown items are ignored.

## Examples

All examples go into the title's settings file.

### 1. Single player (the default)

Nothing to set. Equivalent to:

```toml
max_local_users = 1
```

Every device works for player 1's side as before: keyboard and mouse plus the first gamepad drive player 1.

### 2. Two players, two gamepads

```toml
max_local_users = 2
```

Pad 1 (and the keyboard) is player 1, pad 2 is player 2. Player 2 signs in when the second gamepad is connected.

### 3. Two players, one gamepad: keyboard against the pad

```toml
max_local_users = 2
keyboard_own_player = true
```

Keyboard and mouse are player 1, the gamepad is player 2. Player 2 signs in as soon as the gamepad is connected.

The same with chosen devices:

```toml
max_local_users = 2
input_player1 = "keyboard"
input_player2 = "pad1"
```

### 4. Two players, one gamepad: the pad is player 1

```toml
max_local_users = 2
input_player1 = "pad1"
input_player2 = "keyboard"
```

Useful when player 1 sits at the TV with the gamepad and player 2 plays with keyboard and mouse at the desk. Player 2 is signed in from the start, because the keyboard is always there.

### 5. Four players, four gamepads

```toml
max_local_users = 4
```

Pad N is player N. Each player signs in when their gamepad connects.

### 6. Four players, keyboard plus three gamepads

```toml
max_local_users = 4
keyboard_own_player = true
```

Keyboard and mouse are player 1, pads 1 to 3 are players 2 to 4.

### 7. Swap which gamepad is player 1

The gamepads were connected in the "wrong" order:

```toml
max_local_users = 2
input_player1 = "pad2,keyboard"
input_player2 = "pad1"
```

### 8. Player 2 takes every other gamepad

```toml
max_local_users = 2
input_player1 = "keyboard,pad1"
input_player2 = "pads"
```

Player 1 has keyboard, mouse and pad 1. Every other gamepad drives player 2 (a spare pad can be picked up without changing settings).

### 9. Keyboard for nobody

```toml
max_local_users = 2
input_player1 = "pad1"
input_player2 = "pad2"
```

Keyboard and mouse drive nobody: an accidental key press does not move player 1.

### 10. Named second player

```toml
max_local_users = 2
user_gamertag_2 = "Alex"
```

The game shows "Alex" for player 2 (where it shows names: lobby, scoreboard, join screen). Players 3 and 4 have no name setting: they are `User(3)` and `User(4)`.

### 11. Player 2 always signed in, without a second controller

```toml
max_local_users = 2
input_player1 = "keyboard,pads"
input_player2 = "none"
```

This does not sign player 2 in: a player without a device is not signed in. Some games need two signed-in players before they offer split-screen; give player 2 a device (the keyboard, as in example 4) for that.

## Troubleshooting

- **The game shows one player only.** Check `max_local_users` (at least 2) and that player 2 has a device: in the log, `XAM: local users signed in: 1 -> 3` means players 1 and 2 are signed in. Many games only look for new players on their menu or join screen; connect the gamepad there, or before starting the game.
- **The second gamepad moves player 1.** With chosen devices, player 1 has `pads` or `auto` and takes every pad player 2 does not name. Name player 2's pad (`input_player2 = "pad2"`).
- **The wrong gamepad is player 1.** Pads are numbered by connection order. Connect them in order, or name them (example 7).
- **Players 3 and 4 never sign in.** They exist in automatic mode only; leave `input_player1` and `input_player2` at `auto`.
- **A player's options don't persist.** Their profile settings folder (`profile/User2/` and so on) must be writable; when it isn't, the log says `Can't save profile setting` and the options last for the session only.
- **Both players share a save.** Expected: saves are kept under player 1's XUID for every player.

## Limitations

- At most four local players; chosen devices cover players 1 and 2 only.
- Players' saves are shared (see above). Only title profile settings are per player.
- No online identity: XUIDs and names are local. Online features of a game see the same placeholder accounts.
- There is no per-player gamepad button remapping.
- The game decides how split-screen looks and when it accepts new players; the runtime cannot add split-screen to a game that has none.
