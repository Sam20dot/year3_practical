# 8085 Clock Visualizer

An **Intel 8085 microprocessor simulator** with a built-in assembler, a live CPU view, and a 7-segment clock that is driven by real 8085 machine code.

Type or paste 8085 assembly, press **Assemble**, press **Run**, and watch the registers, flags, memory, T-states and the clock display change. Edit one instruction while it runs and see exactly what that change does. It was built as a Year 3 microprocessor practical, so that "why does it work this way?" and "what if we change this?" can be answered by trying it, not guessing.

![screenshot](docs/screenshot.png)

## What it does

- **Editor and assembler.** Paste code, get machine bytes. Errors show the line number.
- **All 246 documented 8085 opcodes**, with correct flags (S, Z, AC, P, CY) and **real T-state counts** per instruction.
- **Runs at a real 3 MHz clock model.** 3,000,000 T-states is exactly one simulated second, so a delay loop really takes one second.
- **Live CPU view.** Registers in hex, decimal and binary, flags, HL, SP, PC, T-state and instruction counters. Anything that just changed is highlighted.
- **Listing with breakpoints.** The current line follows the PC. Click a line to set a breakpoint.
- **Memory viewer** with the PC and SP marked.
- **7-segment clock display** on I/O ports `10h` (seconds), `11h` (minutes) and `12h` (hours), in BCD.
- **Speed control** from 3 MHz down to 30 Hz, so you can watch every instruction.
- **A terminal edition** (single C file, no dependencies) with a step-by-step "what changed and why" trace.

## How it works

```
 editor text ──► assembler ──► bytes in memory ──► CPU executes ──► ports 10h/11h/12h
      ▲              │                                  │                  │
      └─ error line ─┘                         registers, flags,      7-segment clock
                                               T-states, memory
```

The demo program is a digital clock written in 8085 assembly:

1. It keeps seconds, minutes and hours in registers C, D and E, **in BCD** (binary coded decimal), so `59` is stored as `59H`.
2. Each tick it writes them to ports `10h`, `11h`, `12h` with `OUT`.
3. It calls a **delay loop** that burns exactly one second of CPU time:

   ```
   inner loop:  DCX H (6) + MOV A,H (4) + ORA L (4) + JNZ (10) = 24 T-states
   24 T-states x 62,500 iterations x 2 outer passes = 3,000,000 T-states = 1 second at 3 MHz
   ```
4. It adds 1 to seconds with `ADI 01H` and then `DAA` (decimal adjust), which turns `09H + 1 = 0AH` back into `10H`. When seconds reach `60H` it resets them and adds 1 to minutes, and so on up to `24H` hours.

### T-states of common instructions

| Instruction | T-states |
|---|---|
| `MOV r,r`, `ADD r`, `INR r` | 4 |
| `MVI r,data`, `ADI data`, `MOV r,M` | 7 |
| `DCX rp`, `INX rp` | 6 |
| `JNZ addr` | 10 if the jump is taken, 7 if not |
| `OUT port` | 10 |
| `CALL addr` | 18 |
| `RET` | 10 |

## Quick start

### 1. Install the tools

**Ubuntu / Debian / WSL2:**
```bash
sudo apt update
sudo apt install build-essential cmake pkg-config libglfw3-dev libgl1-mesa-dev
```
**macOS:** `brew install cmake glfw`

On Windows, use **WSL2** (Windows 11 shows the window through WSLg).

### 2. Clone and build

```bash
git clone https://github.com/Sam20dot/year3_practical.git
cd year3_practical/microprocessor/project/8085_clock_visualizer
cmake -B build
cmake --build build
```

### 3. Run

```bash
./build/8085_clock
```

The window opens with the clock demo loaded. Press **Run** and the seconds start counting.

## Using it

| Control | What it does |
|---|---|
| **Assemble** | Turns the editor text into bytes and resets the CPU. |
| **Run / Pause** | Runs at the chosen speed. It stops at `HLT` or a breakpoint. |
| **Step** | Executes one instruction. |
| **Reset** | Registers back to power-on state, PC back to the program start. |
| **Clock sample** | Reloads the clock demo. |
| **Speed** | 3 MHz (real), 300 kHz, 30 kHz, 3 kHz, 300 Hz, 30 Hz. |
| **Auto-assemble** | Re-assembles about 0.4 s after you stop typing (this resets the CPU). |
| Click a listing line | Toggle a breakpoint. |

### Try it: a first program

Click in the editor, select everything, paste this, and press **Assemble** then **Run**:

```
        ORG 0000H
        MVI A,05H      ; A = 5
        MVI B,03H      ; B = 3
        ADD B          ; A = A + B
        HLT
```
The CPU panel should show `A = 08` and the CPU halted.

### Try it: what if we change this?

Edit the clock sample and press **Assemble** to see the effect.

| Change | What you should see |
|---|---|
| Replace `DAA` with `NOP` | The seconds stop being decimal. At `0AH`..`0FH` the digit goes blank, because those values are not BCD digits. |
| `MVI B,02H` to `MVI B,01H` | The clock ticks twice as fast, every 0.5 s (1 x 62,500 x 24 = 1,500,000 T-states). |
| `CPI 60H` to `CPI 30H` (seconds) | The seconds roll over to `00` at 30 and the minute ticks. |
| Change `LXI H,0F424H` | The delay length changes. Use `0F424H` = 62,500 for a 1 second tick. |

## Assembler syntax

```
LABEL:  MNEMONIC operands    ; comment
```

- **Numbers:** `0FFH` or `0xFF` (hex), `255` or `255D` (decimal), `11111111B` (binary), `'A'` (character). A hex number must start with a digit, so write `0FFH`, not `FFH`.
- **Directives:** `ORG addr`, `DB v,v,...`, `DW v,...`, `NAME EQU value`, `END`.
- **Expressions:** `+` and `-` between numbers and labels, and `$` for the current address.
- **Case-insensitive.** Mnemonics, registers and labels can be in any case.
- `MOV` copies register to register. To load a value into a register use `MVI` (for example `MVI A,32H`, not `MOV A,32H`).

## Terminal edition

A single C file with no dependencies, handy for quick experiments and for lectures:

```bash
gcc -O2 -o sim8085 terminal/sim8085.c
./sim8085
```

| Command | What it does |
|---|---|
| `sample` | Load the clock demo. |
| `edit` | Paste code, finish with a line containing only `.` |
| `asm`, `list` | Assemble, show the listing with bytes. |
| `s [n]` | Step. Shows the bytes, T-states, **what changed** and **why**. |
| `run [T]` | Run T T-states (default 3,000,000 = 1 second at 3 MHz). |
| `b ADDR`, `m ADDR`, `dis ADDR` | Breakpoint, memory dump, disassemble. |
| `set REG VAL`, `poke ADDR VAL` | Change a register, flag or memory byte. |
| `patch ADDR code` | Replace one instruction live, e.g. `patch 0020 ADI 02H`. |
| `MVI A,78H` (any instruction) | Type an instruction on its own and it runs immediately. |

All numbers are hex. Example, showing why `DAA` exists:

```
8085> MVI A,78H
8085> ADI 99H      ; A = 11H, AC = 1, CY = 1
8085> DAA          ; A = 77H, CY = 1   (78 + 99 = 177 in decimal)
```

## Project layout

```
8085_clock_visualizer/
├── CMakeLists.txt
├── asm/        8085 assembly programs
├── include/    headers (sim8085.h: the engine's public interface)
├── src/        engine (assembler + CPU) and the GUI (main.cpp)
├── terminal/   single-file terminal edition
└── vendor/     bundled third-party code (glad, Dear ImGui)
```

The engine (`src/`) has no GUI code in it. The GUI and the terminal edition both use the same assembler and CPU.

## Accuracy and limits

- Implements the 246 documented 8085 instructions with Intel T-state counts. Undocumented opcodes act as `NOP`.
- Interrupts and serial I/O (`EI`, `DI`, `RIM`, `SIM`) are accepted but have no effect.
- Flags follow the 8085 data sheet: `ANA` sets AC, `ORA`/`XRA` clear AC and CY, and `INR`/`DCR` leave CY unchanged.
- The editor is a plain text box (no syntax coloring yet).

## Credits

- Inspired by Jubin Mitra's Java *8085 Simulator*.
- Built with [Dear ImGui](https://github.com/ocornut/imgui), [GLFW](https://www.glfw.org/) and [glad](https://github.com/Dav1dde/glad).
- Author: Sam ([@Sam20dot](https://github.com/Sam20dot)), University of Rwanda, Electronics and Telecommunication Engineering.
