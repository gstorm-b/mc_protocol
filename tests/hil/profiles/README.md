# HIL profiles

Example profiles for `tools/hil_capture` (`docs/spec/SPEC-hil-capture.md`, "Profile"). One file
per row of the spec's "Profiles on this bench" table that this folder shows; the other profiles of
a row differ only in the frame settings (data code, serial format, sum check).

| File | PLC and frame |
|---|---|
| `q03ude-eth-3e-bin.example.json` | Q CPU, built-in Ethernet, 3E Binary |
| `q03ude-c24-3c-f4.example.json` | Q, C24, 3C format 4 |
| `fx5u-eth-3e-ascii.example.json` | FX5U, built-in Ethernet, 3E ASCII |
| `fx3-eth-1e-bin.example.json` | FX3, Ethernet adapter, 1E Binary |
| `fx3-serial-1c-f1.example.json` | FX3, serial computer link, 1C format 1 |

**Everything in these files is a placeholder.**

- Addresses are placeholders: the IP address is from TEST-NET-1 (`192.0.2.x`, reserved for
  documentation) and the COM port is `COM1`.
- The models, firmware, device ends and every **scratch area** are guesses, not verified against
  a PLC. A scratch area is only real once the owner has decided it for the PLC at the Checkpoint F
  gate.
- The FX3 `specialBit` / `specialWord` (`M8000` / `D8000`) and the FX3 computer link's formats 1
  and 4 are unverified until the owner has checked the FX3 manual (the open operator-procedure item
  of the spec).

**Real profiles stay local.** They hold IP addresses and COM port names, so `.gitignore` ignores
`tests/hil/profiles/*.json` and keeps only `*.example.json`. Copy an example, edit it and run
`hil_capture --profile` on the copy; the capture's `run.meta` records the frame and serial
settings but never an address, a TCP port or a COM port name: the profile's host, `:port` and COM
name are replaced by `<redacted>` wherever they appear in it, also in the free-text fields (`module`,
`adapter`, `plcState`) and in the operator note.

Every key the schema does not define is rejected, so a typo in a key name cannot silently drop a
safety field.
`hil_capture --dry-run` shows the resolved frames and the safety gate's verdict without connecting.

**Device references in a plan.** `D@s` is the first number of the first scratch range of the type,
`M@s16` the first multiple of 16 inside it, `D@end` the profile's `deviceEnd`. An offset (`+10`,
`-1`) is a plain decimal number added to the resolved device number, also on a hexadecimal device:
with `W100-W1FF` as scratch, `W@s+10` is `W10A` (100H plus ten), not `W110`. A reference that
resolves below zero is an error.
