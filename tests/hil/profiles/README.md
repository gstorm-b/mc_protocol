# HIL profiles

Profiles for `tools/hil_capture` (`docs/spec/SPEC-hil-capture.md`, "Profile"). A profile is one PLC,
one transport and one frame configuration: one set of PLC parameters downloaded to the bench.

## Example profiles (committed)

| File | PLC and frame |
|---|---|
| `q03ude-eth-3e-bin.example.json` | Q CPU, built-in Ethernet, 3E Binary |
| `q03ude-c24-3c-f4.example.json` | Q, C24, 3C format 4 |
| `fx5u-eth-3e-ascii.example.json` | FX5U, built-in Ethernet, 3E ASCII (X,Y HEX) |
| `fx3-eth-1e-bin.example.json` | FX3, Ethernet adapter, 1E Binary |
| `fx3-serial-1c-f1.example.json` | FX3, serial computer link, 1C format 1 |

Their scratch areas, `deviceEnd` and `supports` follow the owner's answers for each PLC
(`temp-docs/hil-scratch-areas.md`, 2026-10-04); where the owner kept the leader's low-confidence guess
(the FX5U W/B ranges and `deviceEnd`, and its X/Y end `1777`, which is not in that file) the value is a
proposal to confirm at the bench: scratch `D100-D2099` and `M100-M2099` on every PLC,
plus `W100-W1FF`, `B100-B1FF` and `Y0-Y7` on the Q, and `W100-W1FF`, `R0-R99`, `B10-B7F` and
`Y0-Y7` on the FX5U. The connection is a placeholder: the IP address is from TEST-NET-1
(`192.0.2.x`, reserved for documentation) and the COM port is `COM1`.

## Bench draft profiles (local, never committed)

The bench has 16 profiles: the 14 of the spec's "Profiles on this bench" table and two for the FX5U
built-in RS-485 port. Their drafts are local files named `<id>.json` in this folder; `.gitignore`
keeps them out of git. Each runs with the plan of its frame family:

| Id | PLC and frame | Plan |
|---|---|---|
| `q03ude-eth-3e-bin` | Q CPU, built-in Ethernet, 3E Binary | `qna_ethernet` |
| `q03ude-eth-3e-ascii` | Q CPU, built-in Ethernet, 3E ASCII | `qna_ethernet` |
| `q03ude-c24-3c-f1-sum` | Q, C24, 3C format 1, sum check on | `qna_serial` |
| `q03ude-c24-3c-f1-nosum` | Q, C24, 3C format 1, sum check off | `qna_serial` |
| `q03ude-c24-3c-f2` | Q, C24, 3C format 2, block number 5AH | `qna_serial` |
| `q03ude-c24-3c-f3` | Q, C24, 3C format 3 | `qna_serial` |
| `q03ude-c24-3c-f4` | Q, C24, 3C format 4 | `qna_serial` |
| `fx5u-eth-3e-bin` | FX5U, built-in Ethernet, 3E Binary | `qna_ethernet` |
| `fx5u-eth-3e-ascii` | FX5U, built-in Ethernet, 3E ASCII (X,Y HEX) | `qna_ethernet` |
| `fx3-eth-1e-bin` | FX3, Ethernet adapter, 1E Binary | `a1e` |
| `fx3-eth-1e-ascii` | FX3, Ethernet adapter, 1E ASCII | `a1e` |
| `fx3-serial-1c-f1-sum` | FX3, computer link, 1C format 1, sum check on | `a1c` |
| `fx3-serial-1c-f1-nosum` | FX3, computer link, 1C format 1, sum check off | `a1c` |
| `fx3-serial-1c-f4` | FX3, computer link, 1C format 4 | `a1c` |
| `fx5u-rs485-3c-f1-sum` | FX5U, built-in RS-485, 3C format 1, sum check on | `qna_serial` |
| `fx5u-rs485-3c-f4` | FX5U, built-in RS-485, 3C format 4 | `qna_serial` |

The plans are `tests/hil/plans/<plan>.json`. Every draft passes `hil_capture --dry-run` with its plan.

**The `FILL:` convention.** A draft holds the owner's decided values and, where the owner left the
leader's guess, that guess. Everything the owner still has to fill in or confirm is written in the
free-text fields `firmware`, `adapter` and `plcState`, as text starting with `FILL:` (the loader
refuses unknown keys, so there is no separate field for it). Find them all with
`Select-String -Path tests/hil/profiles/*.json -Pattern "FILL:"`. They name, per profile:

- the firmware or function versions of the CPU and module;
- the adapter (USB-serial model and latency timer) and the real connection: the TCP host and port in
  `device.transport.tcp`, or the COM port and line in `device.transport.serial`;
- the PLC state and what to confirm before writing: that `Y0-Y7` drive no load (Y is in scratch by
  the owner's default), and the low-confidence values (FX5U scratch W/B, `deviceEnd` and
  `scanTimeDevice`; FX3 CPU model and `scanTimeDevice`).

**Filling a draft.**

1. Copy it if you want to keep the draft (`Copy-Item q03ude-eth-3e-bin.json my-q03ude-eth-3e-bin.json`,
   and change `profile.id`), or edit it in place.
2. Set the real host and port, or COM port and line settings, in `device.transport`.
3. Check every value a `FILL:` names against the PLC parameters, then replace the `FILL:` text with
   the real description (firmware, adapter, PLC state).
4. Download the matching PLC parameters with GX Works (code, format, sum check, station, MC protocol
   port, online change).
5. `hil_capture --profile tests/hil/profiles/<id>.json --plan tests/hil/plans/<plan>.json --dry-run`
   and read the frames and the safety gate's verdict; then run without `--dry-run`.

**Real profiles stay local.** A filled profile holds IP addresses and COM port names: it is never
committed and never published. `.gitignore` ignores `tests/hil/profiles/*.json` and keeps only
`*.example.json`. The capture's `run.meta` records the frame and serial settings but never an
address, a TCP port or a COM port name: the profile's host, `:port` and COM name are replaced by
`<redacted>` wherever they appear in it, also in the free-text fields (`module`, `adapter`,
`plcState`) and in the operator note.

## Keys worth knowing

Every key the schema does not define is rejected, so a typo in a key name cannot silently drop a
safety field. `hil_capture --dry-run` shows the resolved frames and the safety gate's verdict without
connecting.

**Device references in a plan.** `D@s` is the first number of the first scratch range of the type,
`M@s16` the first multiple of 16 inside it, `D@end` the profile's `deviceEnd`. An offset (`+10`,
`-1`) is a plain decimal number added to the resolved device number, also on a hexadecimal device:
with `W100-W1FF` as scratch, `W@s+10` is `W10A` (100H plus ten), not `W110`. A reference that
resolves below zero is an error.

**Special range (`specialFrom`).** The FX3 profiles declare `"specialFrom": {"D": 8000, "M": 8000}`:
past `deviceEnd` (D7999, M7679) the FX3 has its special registers and relays from D8000 and M8000,
which exist. A read that lies in that range and expects a PLC error runs expecting ok instead
(catalogue G5-02 reads D8000); a read that spans the general and the special range (G5-03, D7999 and
D8000) is not sent and is reported as skipped with the reason. The dry run and the run summary show
both. Q and FX5U profiles omit the key: their special devices are the separate types SM and SD.

**X/Y numbering.** An FX CPU numbers X and Y in octal. The FX profiles set the device config's
`frame.xyNotation` to `"Octal"`: scratch ranges, `deviceEnd`, `specialFrom`,
`specialBit`/`specialWord`, `scanTimeDevice` and the literal devices of a plan (`Y20`, `X7`) are
then written in octal (so `Y20-Y37` is 16 points, indices 16 to 31, and a `deviceEnd` of X or Y is a
JSON string such as `"377"`), and the tool's texts (dry run, gate messages, `run.meta`, capture
records) write X and Y the same way. Offsets and alignments stay plain decimal counts of points.
`frame.xyAsciiDigits` is what the PLC expects inside ASCII frames: `"Hex"` for the FX3 1E adapter and
the FX5U (Binary, or the "ASCII (X,Y HEX)" communication data code), `"Octal"` for the FX3 computer
link (1C).
