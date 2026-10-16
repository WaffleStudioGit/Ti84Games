# SPIRE

A climb for the TI-84 Plus CE. Ten floors, one screen each. The clock runs until you reach the top.

You run, jump, wall-jump, and dash. Hold the jump to go higher. Dash in the direction you are holding, including up. The dash comes back when you touch the ground or a wall. Down and jump drops through a thin platform.

## Keys

| Key | Action |
| --- | --- |
| Arrows | Move |
| 2nd | Jump |
| Alpha | Dash |
| Del | Retry the floor |
| Clear | Leave |
| Enter | Start, and start again |

A clean climb is ranked above a fast one. Your best is kept in the appvar `SPIRESV`.

## On the calculator

`dist/SPIRE.8xp` is the program. Send it with TI Connect CE or TiLP.

OS 5.4 and earlier run assembly programs directly. On OS 5.5 and later, assembly is switched off until [arTIfiCE](https://yvantt.github.io/arTIfiCE/) is installed. After AsmHook2 is in place, run `SPIRE` from the program menu. The program is archived, so a RAM clear does not delete it.

## Build

The CE C toolchain (CEdev 15) builds the calculator program:

```
export PATH="$HOME/CEdev/bin:$PATH"
make
```

That writes `bin/SPIRE.8xp`.

A desktop preview uses SDL2. Arrows move, Z jumps, X dashes, R retries, Esc leaves, Enter starts. F1 skips a floor.

```
sudo apt install libsdl2-dev libsdl2-ttf-dev
make -f host.mk
./spire-host
```

`make -f host.mk spire-selftest && ./spire-selftest` checks that every floor can be finished and that the long pit actually requires a dash.
