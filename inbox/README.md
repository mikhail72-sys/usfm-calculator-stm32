# inbox — incoming asks from neighbour projects

Neighbour projects — the USFM effort in `C:\Piezus\TI\usfm` (lead,
analyzer, device, cheater), BlueUsbSerial, hartcalibr, the config tool,
... — drop their "the problem is on your side" notes here: bugs,
questions, feature requests aimed at the STM32 calculator firmware.

Rules (same convention as uslm5lp0v3 / BlueUsbSerial / TI usfm):

- One ask = one file `YYYY-MM-DD_<from>_<short-slug>.md`
  (the older `YYMMDD_slug.md` form of BlueUsbSerial is accepted too).
- A neighbour writes it; the OWNER of this repo answers and closes —
  the reply is appended to the same file, the status flips in the header.
- Statuses: `open` -> `taken` -> `answered` / `done` / `wontfix`.
  Files are never deleted — history is cheap.
- Every session in this repo starts by checking for `status: open` here.
- Cross-branch agreements (protocol, register map, frame formats) live as
  `AGREED_*` files on the owner's side; the canonical protocol header for
  the meter is `C:\Piezus\TI\usfm\protocol.h`.

Ask template:

```markdown
# Short problem title

from:   lead
date:   2026-09-03
status: open

## Symptom
What is seen from outside, verbatim (log, output, screenshot).

## Repro
Minimal steps: commands, config, hardware.

## Expectation
What should happen / what answer is needed.
```
