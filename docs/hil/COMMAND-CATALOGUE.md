# HIL Command Catalogue

- **Status:** approved by the owner, 2026-09-26
- **Belongs to:** `docs/spec/SPEC-hil-capture.md` (module `hil-capture`)
- **Use:** the list of everything the capture tool sends to a real PLC, per frame family. The plan files `tests/hil/plans/*.json` are the machine form of this list; step ids are identical.

## How to read this catalogue

**Frame families.** `3E` = QnA over Ethernet, `1E` = A-compatible over Ethernet, `3C` = QnA over serial (C24), `1C` = A-compatible over serial. A column marked ✓ runs the step for that family; `—` skips it.

**Device references** resolve against the profile:

| Reference | Meaning |
|---|---|
| `D@s`, `D@s+10` | First scratch address of type D, plus an offset |
| `M@s16` | First scratch M address aligned up to a multiple of 16 |
| `D@end`, `D@end+1` | Last configured D number (`deviceEnd`), and the first one that does not exist |
| `D100` | A literal address (the `GV` group only) |

**Limits** come from `mc::maxPoints()` for the profile's frame, code and target family, as in `SPEC-core-model.md`:

| Limit name | 3E Binary | 3E ASCII | 1E | 3C | 1C |
|---|---|---|---|---|---|
| `Wmax`: word read/write, word device | 960 | 960 | 256 | 960 | 64 |
| `BRmax`: bit read | 7168 | 3584 | 256 | 7904 | 256 |
| `BWmax`: bit write | 7168 | 3584 | 256 | 7904 | 160 |
| `WBRmax`: word read of a bit device | 960 | 960 | 128 | 960 | 32 |
| `WBWmax`: word write to a bit device | 960 | 960 | 40 | 960 | 10 |

(3E/3C figures are the `IqR_Q_L` column used for Q CPUs. An FX5 profile uses whatever target family its profile declares; if the PLC rejects a request at the limit, that is a finding.)

A step whose count exceeds the scratch area is **skipped** with `skipped: scratch too small`, not shrunk: the point of a limit step is the limit.

**Expectations:** `ok`, `ok = values`, `plcError` (code recorded), `timeout`, `noResponse`, `notSent` (the library must refuse before sending), `record` (probe: whatever happens is the result).

**Where each group runs:**

| Group | Runs in |
|---|---|
| G0, GV, G1, G2, G3, G6, GB | every profile |
| G4, G5, G7, G9 | once per PLC × frame family (the first profile of that family on that PLC) |
| G8 | only the profiles each probe names |

## Profiles on this bench (decided 2026-09-26)

Fourteen profiles, one parameter download each (see the spec for the coverage consequences):

| PLC / module | Frame | Profiles |
|---|---|---|
| Q CPU, built-in Ethernet | 3E | Binary · ASCII |
| Q, C24 | 3C | F1 sum on · F1 sum off · F2 (block 5AH) · F3 · F4 |
| FX5U, built-in Ethernet | 3E | Binary · ASCII |
| FX3, Ethernet adapter | 1E | Binary · ASCII |
| FX3, serial / computer link | 1C | F1 sum on · F1 sum off · F4 |

`SB` / `SW` below mean the profile's `specialBit` / `specialWord` (Q and FX5 over 3E/3C: `SM0` / `SD0`; FX3 over 1E/1C: FX3's own special range, addresses from its manual).

---

## G0 — Smoke

| ID | Step | 3E | 1E | 3C | 1C | Expect |
|---|---|---|---|---|---|---|
| G0-01 | Connect; read `D@s` × 1 | ✓ | ✓ | ✓ | ✓ | ok |
| G0-02 | Read the profile's `scanTimeDevice` × 1 (if set) | ✓ | ✓ | ✓ | ✓ | ok, value recorded in `run.meta` |

## GV — Appendix A mirror

Needs D100–D102 and M100–M107 in scratch and the default route / station of the vectors. Writes first, so the PLC holds the vector's values; then the exact vector request. RPL-05 compares bytes with the named vectors.

| ID | Step | 3E | 1E | 3C | 1C | Mirrors |
|---|---|---|---|---|---|---|
| GV-01 | Write D100 × 3 = 1995H, 1202H, 1130H | ✓ | ✓ | ✓ | ✓ | V-3E-x-05/06, V-1E-x-05/06, V-1Cn-05/07 (3C: none, recorded) |
| GV-02 | Read D100 × 3 → 1995H, 1202H, 1130H | ✓ | ✓ | ✓ | ✓ | V-3E-x-01/02, V-1E-x-01/02, V-3Cn-01/02, V-1Cn-01/02 |
| GV-03 | Write M100 × 8 = 1,1,0,0,1,1,0,0 | ✓ | ✓ | ✓ | ✓ | V-3E-x-07, V-1E-x-07/08, V-3Cn-03/04, V-1Cn-06/07 |
| GV-04 | Write M100 × 8 = 0,0,0,1,0,0,1,1; read M100 × 8 | ✓ | ✓ | ✓ | ✓ | V-3E-x-03/04, V-1E-x-03/04, V-1Cn-03/04 (3C: none) |
| GV-05 | Write M100 × 5 = 1,0,1,0,1; read M100 × 5 (odd count) | ✓ | ✓ | ✓ | ✓ | V-3E-x-08/09, V-1E-x-09/10 (others: none) |

`x` = B or A by code; `n` = the serial format.

## G1 — Word devices

| ID | Step | 3E | 1E | 3C | 1C | Expect |
|---|---|---|---|---|---|---|
| G1-01 | Write `D@s` × 1 = 1234H; read back | ✓ | ✓ | ✓ | ✓ | ok = 1234H |
| G1-02 | Write `D@s` × 7 (odd) = 0001H…0007H; read back | ✓ | ✓ | ✓ | ✓ | ok = values |
| G1-03 | Write `D@s` × `Wmax`, word k = (k × 0101H) & FFFFH; read back × `Wmax` | ✓ | ✓ | ✓ | ✓ | ok = values (limit accepted by the PLC) |
| G1-04 | Ad-hoc read `D@s` × (`Wmax` + 40) through `McDevice` | ✓ | ✓ | ✓ | ✓ | ok; 2 frames on the wire; payload = G1-03 values then 40 more |
| G1-05 | Write `W@s` × 4 (hex radix) = FFFFH, 8000H, 0001H, 0000H; read back | ✓ | ✓ | ✓ | ✓ | ok = values |
| G1-06 | Write `R@s` × 4; read back (if R in `supports`) | ✓ | ✓ | ✓ | ✓ | ok = values |
| G1-07 | Write `ZR@s` × 4; read back (if ZR in `supports`) | ✓ | — | ✓ | — | ok = values |
| G1-08 | Read `TN0` × 4 and `CN0` × 4 (current values; write only if TN/CN are in scratch) | ✓ | ✓ | ✓ | ✓ | ok, record |
| G1-09 | Read special registers `SW` × 4 | ✓ | ✓ | ✓ | ✓ | ok, record |
| G1-10 | Float and dword helpers: write `D@s+20` × 4 = `convert::fromFloat64(1.5)`, `D@s+24` × 2 = `fromInt32(-2)`; read back; decode with `convert` | ✓ | ✓ | ✓ | ✓ | 1.5 and −2 (word order on real hardware) |

## G2 — Bit devices, bit units

| ID | Step | 3E | 1E | 3C | 1C | Expect |
|---|---|---|---|---|---|---|
| G2-01 | Write `M@s` × 1 = 1; read × 1; write 0; read | ✓ | ✓ | ✓ | ✓ | ok = 1, then 0 |
| G2-02 | Write `M@s` × 16 = 1010…10; read × 16 | ✓ | ✓ | ✓ | ✓ | ok = values |
| G2-03 | Write `M@s` × 5 = 1,0,1,0,1 (odd); read × 6 (`M@s+5` pre-cleared) | ✓ | ✓ | ✓ | ✓ | ok; point 6 stays 0 (padding nibble / dummy char has no effect) |
| G2-04 | Write `M@s` × `BWmax` alternating; read × `BRmax` (or `BWmax` if smaller) | ✓ | ✓ | ✓ | ✓ | ok = values |
| G2-05 | Write `B@s` × 16 (hex radix); read back | ✓ | ✓ | ✓ | ✓ | ok = values |
| G2-06 | Write `Y@s` × 8; read back (only if Y is in scratch) | ✓ | ✓ | ✓ | ✓ | ok = values |
| G2-07 | Write/read × 4 each of L, F, S (only the types in `supports` and in scratch) | ✓ | ✓ | ✓ | ✓ | ok = values |
| G2-08 | Read `X0` × 16 and × 64 (inputs, never written) | ✓ | ✓ | ✓ | ✓ | ok, record |
| G2-09 | Read `TS0`, `TC0`, `CS0`, `CC0` × 8 | ✓ | ✓ | ✓ | ✓ | ok, record |
| G2-10 | Read special relays `SB` × 16 | ✓ | ✓ | ✓ | ✓ | ok, record |

## G3 — Bit devices in word units

| ID | Step | 3E | 1E | 3C | 1C | Expect |
|---|---|---|---|---|---|---|
| G3-01 | Write words `M@s16` × 2 = 1234H, 0002H; read bits `M@s16` × 32 | ✓ | ✓ | ✓ | ✓ | ON at +2, +4, +5, +9, +12, +17 only (spec §2.4, PRIM-15) |
| G3-02 | Read words `X0` × 4; compare with G2-08 bits | ✓ | ✓ | ✓ | ✓ | ok, consistent |
| G3-03 | Read words `M@s16+3` × 1 (head not a multiple of 16) | ✓ ok | notSent | ✓ ok | notSent | Q CPUs accept; 1E/1C refused client-side (DEV-12) |
| G3-04 | Read words `SB` × 1 (special relays in word units; `SB` must be a multiple of 16) | ✓ | ✓ | ✓ | ✓ | ok, record |
| G3-05 | Read words `M@s16` × `WBRmax`; write words × `WBWmax` | ✓ | ✓ | ✓ | ✓ | ok (limits accepted) |

## G4 — Device-code sweep (once per PLC × family)

| ID | Step | 3E | 1E | 3C | 1C | Expect |
|---|---|---|---|---|---|---|
| G4-01 | For every type in `supports`: read 1 point at number 0 (bit read for bit types, word read for word types) | ✓ | ✓ | ✓ | ✓ | ok (every device code accepted) |
| G4-02 | For every type the family's code table has but `supports` does not: the same read | ✓ | ✓ | ✓ | ✓ | plcError (record code) or ok (record: the table is wider than the profile said) |
| G4-03 | 1E only, with `e1AliasLS`: read L0 and S0 (encoded as M) | — | ✓ | — | — | record |
| G4-04 | 1C only: the same reads with the AnA command set (JR/QR) | — | — | — | ✓ | record (does this PLC accept AnA commands?) |

## G5 — Boundaries and PLC errors (once per PLC × family)

| ID | Step | 3E | 1E | 3C | 1C | Expect |
|---|---|---|---|---|---|---|
| G5-01 | Read `D@end` × 1 | ✓ | ✓ | ✓ | ✓ | ok |
| G5-02 | Read `D@end+1` × 1 | ✓ | ✓ | ✓ | ✓ | plcError: end code; 3E error information; 1E abnormal code if 5BH |
| G5-03 | Read `D@end` × 2 (crosses the end) | ✓ | ✓ | ✓ | ✓ | plcError |
| G5-04 | Read `M@end+1` × 1 bit | ✓ | ✓ | ✓ | ✓ | plcError |
| G5-05 | If `deviceEnd.M + 1` is not a multiple of 16: subscribe the last few M points with `bitsAsWords` on, one round | ✓ | ✓ | ✓ | ✓ | chunk fails every round (confirms core-session open question 4 on hardware) |
| G5-06 | Write while the PLC forbids it (profile variant with online change disabled, PLC in RUN): write `D@s` × 1 | ✓ | ✓ | ✓ | ✓ | plcError (record code) |

## G6 — Polling session through `McDevice`

One `poll` step; the whole transcript goes to `session.vec` (RPL-04 replays it).

| Phase | Action | Expect |
|---|---|---|
| Setup | Heartbeat on at `M@s+200`; subscribe `D@s` × 64, `D@s+100` × 10 (gap 36), `M@s` × 64, `X0` × 32, `W@s` × 16 | plan: D gap merged or not per `autoGap` (recorded) |
| Round 1 | — | no `valuesChanged`; one `snapshotReady` per type in `DeviceType` order; `cycleDone(1)` |
| After round 2 | Ad-hoc write `D@s+5` = 1234H and `M@s+3` = 1 | two `requestFinished(ok)` |
| Round 3 | — | `valuesChanged` for exactly `D@s+5` and `M@s+3`; heartbeat bit toggled every round |
| After round 4 | Subscribe `B@s` × 16; unsubscribe `W@s` | — |
| Round 5–6 | — | B first read silent, snapshot B present; no W snapshot |

## G7 — Link faults and recovery (once per PLC × family)

| ID | Step | 3E | 1E | 3C | 1C | Expect |
|---|---|---|---|---|---|---|
| G7-01 | `mutate`: a valid read with its last byte removed (truncated request) | ✓ | ✓ | ✓ | ✓ | record PLC behaviour; client: timeout → Ethernet `LinkFault` + reconnect, serial EOT + flush; the next read succeeds |
| G7-02 | `mutate`: 3E subheader 50H → 51H; 1E command code → 7FH | ✓ | ✓ | — | — | record (silence, error or close) |
| G7-03 | `mutate`: wrong SUM in a read request | — | — | ✓ | ✓ | NAK with an error code (record); link stays usable |
| G7-04 | `mutate`: station number + 1 | — | — | ✓ | ✓ | noResponse (multidrop behaviour) |
| G7-05 | `raw`: EOT (F4: EOT CR LF) while idle, then a normal read | — | — | ✓ | ✓ | read ok (C24 back in command-wait state) |
| G7-06 | Pull the cable during G6 round 3, plug back after 5 s (manual, prompted) | ✓ | ✓ | ✓ | ✓ | Ethernet: `linkStateChanged(Disconnected, PeerClosed or TransportError)` or `linkFault`; serial: timeouts → `LinkFault` after 3; tool reconnects; round 1 restarts |

## G8 — Probes for the reference spec's open questions (§10.1)

Each probe answers one question; the answer goes to `FINDINGS.md` whatever it is.

| ID | Question | Profiles | Step | Observe |
|---|---|---|---|---|
| G8-Q1 | F3 short responses carry a SUM? | 3C F3 (sum on); 1C F3 not on this bench | Write `D@s` × 1 (response without data); read `D@end+1` (QNAK / NN) | Bytes after ETX: none (as printed) or 2 SUM characters. Run with `f3ShortResponseHasSum` false and look at the raw capture |
| G8-Q2 | F2 response echoes the block number? | 3C F2 with block 5AH; 1C F2 not on this bench | Read `D@s` × 1; then `mutate` request block to 5BH | Response block number equal to the request's? |
| G8-Q3 | 1E ASCII odd bit write needs a dummy character? | 1E ASCII | (a) library: write `M@s` × 5 (5 characters); (b) `mutate`: append "0" (6 characters); read `M@s` × 6 after each | Which one the PLC accepts; whether the 6th point changes |
| G8-Q4 | Binary odd bit write: padding nibble ignored? | 3E Binary, 1E Binary | `mutate` G2-03 frame: last data nibble 0 → 1; read `M@s` × 6 | `M@s+5` stays 0 (ignored) or becomes 1 |
| G8-Q5 | 3E response route equals the request route? | every 3E profile | Automatic on every 3E response; plus one read with `station` = 01H if the network has another station | Route bytes identical? |
| G8-Q7 | 1E limits (PDF Appendix 5 only) | 1E Binary (FX3: may differ from the A-series figures; either way a finding) | Read words `M@s16` × 128 (ok); `mutate` points to 129; write words to a bit device × 40 (ok) and `mutate` to 41; read `D@s` × 256 (points `00`) | Limit accepted, limit + 1 rejected (record codes) |
| G8-Q8 | Word access M9008 accepted on 1E/1C? | **none on this bench** (needs a CPU exposing M9000–M9255 through 1E/1C) | Kept for a future bench: read words `M9000` × 1; `mutate` head to M9008; read words `M9016` × 1 | M9008 accepted or rejected |
| G8-Q6 | Error-code meanings | all | nothing to run | every `plcError` of the run is copied into the FINDINGS error-code table with its context |

## G9 — Ahead-of-scope captures (optional, once per PLC × family)

Recorded only; no library code uses them yet. They become the first real vectors for v1.1 (random access) and v2 (4E/4C, pipelining).

| ID | Step | 3E | 1E | 3C | 1C |
|---|---|---|---|---|---|
| G9-01 | `raw` 0403 random read: `D@s`, `D@s+10`, `M@s16` (words), `D@s+20` (dword) | ✓ | — | ✓ | — |
| G9-02 | `raw` 1402 random write words and bits inside scratch, then batch reads | ✓ | — | ✓ | — |
| G9-03 | `raw` 1E 04H / 05H random write inside scratch | — | ✓ | — | — |
| G9-04 | `raw` 1C BT / WT inside scratch | — | — | — | ✓ |
| G9-05 | `raw` 0401 with iQ-R subcommand 0002 / 0003 (read `D@s` × 1, `M@s` × 8) | ✓ | — | ✓ | — |
| G9-06 | `raw` two 3E read frames in one TCP write (pipelining probe) | ✓ | — | — | — |
| G9-07 | `raw` the same read as 4E (serial 1234H) if the module has 4E | ✓ | — | — | — |

## GB — Timing benchmark (every profile)

200 repetitions each (1 warm-up discarded), spaced by `cycleIntervalMs`. Times recorded per repetition: `ttfb`, `rx`, `rtt` (see the spec). Runs twice per profile: with the PLC in RUN (with every other group) and again alone with the PLC in STOP. Report: `docs/hil/BENCH.md`.

| ID | Request |
|---|---|
| GB-01 | Read `D@s` × 1 |
| GB-02 | Read `D@s` × 64 |
| GB-03 | Read `D@s` × `Wmax` |
| GB-04 | Read `M@s` × 64 in bit units |
| GB-05 | Read `M@s16` × 4 in word units (the same 64 points: bit versus word cost) |
| GB-06 | Read `M@s` × `BRmax` in bit units |
| GB-07 | Write `D@s` × 1 |
| GB-08 | Write `D@s` × 64 |
| GB-09 | Write `M@s` × 1 bit |
| GB-10 | One polling round of the G6 subscription set (round duration from `cycleDone`) |
| GB-11 | 1C only: GB-02 with message wait 0 and with 3 (30 ms) |
