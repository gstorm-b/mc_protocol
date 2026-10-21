# HIL Findings

- **Belongs to:** `docs/spec/SPEC-hil-capture.md` (decision H3)
- **Rule:** every difference between a real PLC and the reference spec (`docs/mc_reference/mc-protocol-frame-spec.md`) or its golden vectors gets an entry here. While an entry is `open`, its step ids are listed in the profile's `tests/vectors/captured/<profile>/divergences.txt` and the replay test reports them as `known-divergence`. The owner decides each entry; golden vectors from Appendix A are never edited, only tagged.

## Questions to answer on hardware

Pre-filled from the reference spec sec. 10.1 and ideas sec. 5; answered from the 15 captures imported so far (see "Where the evidence is").

| Question | Source | Answered by | Status |
|---|---|---|---|
| F3 short responses (`QACK`/`GG` without data, `QNAK`/`NN`) carry a SUM? | Â§10.1 Q1 | G8-Q1 (3C only on this bench) | **observed: no SUM** - F-004 |
| F2 response echoes the request's block number? | Â§10.1 Q2 | G8-Q2 (3C only on this bench) | **observed: yes** - F-005 |
| 1E ASCII odd bit write: N characters or N + dummy? | sec. 10.1 Q3 | G8-Q3, GV-05 | **observed: N + dummy `'0'`** - F-001 (codec changed). The re-capture with the dummy answers every odd write it reached (GV-05, G2-01, G2-03: `ok`, read back); the G8-Q3 step itself ran on a dead link (F-012) |
| Binary odd bit write: padding nibble ignored? | Â§10.1 Q4 | G8-Q4, G2-03 | **observed:** the Q03UDE ignores it; the FX5U and the FX3U-ENET-ADP close the connection - F-006 |
| 3E normal response route equals the request route? | Â§10.1 Q5 | G8-Q5 | **observed: yes** for the default route (312 of 312 normal 3E responses); a non-default station is only answered with an error (`C05F`) by the Q03UDE - F-007 |
| Error-code meanings | sec. 10.1 Q6 | every plcError (table below) | **table filled**; meanings found for the FX3 1E codes, the FX3 1C NAK codes (02H, 03H, 06H) and the FX5 serial codes 7F24H / 7F40H; the Q-series codes and the other FX5 codes have none in the manual dumps |
| 1E point limits (PDF Appendix 5 only) | Â§10.1 Q7 | G8-Q7, G1-03, G3-05 (on FX3) | **observed:** the FX3U-ENET-ADP rejects 256-word requests and a 256-bit write (`57H`), and a 41-word write to a bit device closes the connection - F-003. G8-Q7 and G8-Q7-R (the "limit accepted" side) were skipped ("scratch too small"), so the exact limits are the manual's, not measured |
| M9008 accepted for word access on 1E/1C? | Â§10.1 Q8 | G8-Q8 | **not testable on this bench** (no CPU exposing M9000â€“M9255 through 1E/1C); the spec rule 9000 + 16k stays |
| 3E Binary gives correct bytes on a real PLC | ideas Â§5 | GV-01â€¦05 on a 3E Binary profile | **observed: yes.** RPL-05 (b) (request and default-value response equal Appendix A) reports no difference on `q03ude-eth-3e-bin`, `q03ude-eth-3e-ascii`, `fx5u-eth-3e-bin` and `fx5u-eth-3e-ascii` |
| 1C and 3C work on the wire (never verified by the old app) | ideas sec. 5 | GV, G1, G2 on serial profiles | **3C observed** on the Q03UDE + QJ71C24N (formats 1 with and without sum, 2, 3, 4) and, format 1 with sum only, on the FX5U built-in RS-485. **1C observed** on the FX3U + FX3U-232-BD: format 1 with and without sum, and format 4 (`fx3-serial-1c-f1-sum`, `-f1-nosum`, `-f4`); RPL-02 (every response parses to its recorded outcome) has no difference in the three profiles, the differences are the mock's (F-010, F-022). 1C formats 2 and 3 stay **wire-unverified** (not captured) |
| `bitsAsWords` past the end of a bit device range fails every round | core-session open question 4 | G5-05 | **not run:** skipped in all 15 captures ("requires endNotMultiple16:M") |

## Where the evidence is

All paths are under `tests/vectors/captured/`. Each folder has `run.meta`, `steps.vec`, `session.vec`, `bench.csv` and `divergences.txt`; the `FILL:` notes in `run.meta` are the owner's and are unchanged. A "capture id" is the `# id:` line of `steps.vec`, `CAP-<profile>-<step>`; a step id with a suffix (`G2-01.3`, `G7-01+2`) belongs to its base step, and `divergences.txt` tags base steps.

| Profile | PLC / module | Frame |
|---|---|---|
| `q03ude-eth-3e-bin`, `q03ude-eth-3e-ascii` | Q03UDECPU, built-in Ethernet | 3E |
| `q03ude-c24-3c-f1-sum`, `-f1-nosum`, `-f2`, `-f3`, `-f4` | Q03UDECPU + QJ71C24N | 3C |
| `fx5u-eth-3e-bin`, `fx5u-eth-3e-ascii` | FX5U-32M, built-in Ethernet | 3E |
| `fx3-eth-1e-bin`, `fx3-eth-1e-ascii` | FX3U-32M + FX3U-ENET-ADP | 1E (re-captured 2026-10-15 with the library that sends the odd-write dummy) |
| `fx3-serial-1c-f1-sum`, `-f1-nosum`, `-f4` | FX3U-32M + FX3U-232-BD (computer link) | 1C (format 1 with and without sum, format 4) |
| `fx5u-rs485-3c-f1-sum` | FX5U-32M, built-in RS-485 | 3C (format 1, with sum) |

Manual quotes come from the text dumps of the vendor PDFs made for T-089 (`build/_scratch-leader/fx3-enet.txt` = FX3U-ENET-ADP manual, `fx5-eth.txt` = FX5 Ethernet manual, `fx3-dc.txt` = FX Series data communication manual (computer link, 1C), `fx5-serial.txt` = FX5 serial communication manual (MC protocol, 3C); line numbers in those dumps). Where the dumps give no meaning, the entry says "meaning not found in the manuals". Status words: `decided` (the owner chose; codec or defaults changed), `decided - keep` (the PLC agrees with the spec, behaviour kept), `open` (no owner decision yet).

## Findings

One entry per observed difference or confirmed behaviour. Ids are never reused.

<!--
### F-001 â€” <one-line title>
- **Profile / step:** q03ude-c24-3c-f3 / G8-Q1
- **Spec says:** â€¦
- **PLC does:** â€¦ (capture: tests/vectors/captured/q03ude-c24-3c-f3/steps.vec, id CAP-â€¦)
- **Status:** open | decided
- **Decision (owner, date):** change codec | change FrameConfig default | keep, document | â€¦
-->

### F-001 - 1E ASCII odd bit write needs one dummy '0'
- **Profile / step:** `fx3-eth-1e-ascii` / GV-05, G2-01, G2-03, G8-Q3 of the 2026-10-14 capture (and the poll steps G6, G7-06, whose ad-hoc one-bit write is one character longer now). No step of the current capture is tagged.
- **Spec says:** (before 2026-10-14) Â§10.1 Q3: send exactly N characters for N points. Now: append one dummy `'0'` after an odd number of points.
- **PLC does:** (figures from the 2026-10-14 capture, tool 0.1.2, replaced by the re-capture below) the FX3U-ENET-ADP gives no answer to an odd bit write without the dummy. `CAP-fx3-eth-1e-ascii-GV-05`: `WriteBits M100 x5`, request `30 32 46 46 30 30 30 41 34 44 32 30 30 30 30 30 30 30 36 34 30 35 30 30 31 30 31 30 31` (5 data characters `31 30 31 30 31`), outcome `timeout`, `waited_ms: 3501.133`; the read-back `GV-05.2-R` is `38 30 30 30 30 30 30 31 30 30` = bits `0 0 0 1 0 0` (the old M100 pattern), so the write was not applied. The same for `G2-01` (3501.164 ms), `G2-01.3` (3501.826), `G2-03` (3502.308), `G2-03.2` (3501.381) and `G8-Q3` (3500.985). Even counts (GV-03/GV-04 M100 x8, G2-02 x16) are answered. Manual, FX3U-ENET-ADP "7.5.3 Batch write in bit units", ASCII caution: "If the number of device points to be set is an odd number, add one byte of dummy data (30H) at the end of data written" (`fx3-enet.txt:5051-5052`; binary: 4 bits of dummy data, lines 5004-5005). Poll steps: `session.vec` G6 tx frames end `... 30 31 30 30 31` (one data character).
- **Replay effect (what is tagged):** the codec now sends the dummy (T-087), so RPL-01 cannot re-encode these requests (the message reads "the mock cannot decode the captured request ..."), and RPL-02/RPL-03 of the same steps differ; RPL-04 of G6 and G7-06 differs by one character (captured 25 bytes, replay 26). Tagged until the profile was re-captured; the re-capture removes the tags.
- **Re-capture (2026-10-15, tool 0.1.3):** the library sends the dummy. `CAP-fx3-eth-1e-ascii-GV-05` (`WriteBits M100 x5`) ends `... 30 35 30 30 31 30 31 30 31 30`: five data characters `31 30 31 30 31` and the dummy `30`; the PLC answers `38 32 30 30` (`ok`, 10.402 ms) and the read-back `CAP-fx3-eth-1e-ascii-GV-05.2-R` is `38 30 30 30 31 30 31 30 31 30` (bits 1 0 1 0 1). `G2-01` (one bit, written twice) and `G2-03` (M105 x1, M100 x5) are answered `ok` and read back correctly. The odd write of `G8-Q3` itself was not answered by the PLC: it ran on a dead link (`transportError LinkDown`, F-012). The no-tags replay of the profile has no RPL-01 difference on an odd write and no RPL-04 difference on G6 / G7-06; F-001 is no longer needed in `divergences.txt`.
- **Status:** decided
- **Decision (owner, 2026-10-14):** change the codec: append the dummy `'0'` (T-087). Re-capture `fx3-eth-1e-ascii`; the tags go away then. The current `fx3-eth-1e-ascii` capture is committed as it is and is replaced after a re-capture with the T-087 codec (owner, 2026-10-14). Done: the 2026-10-15 re-capture is imported and its tags are gone.

### F-002 - FX5U over 3E ASCII accepts fewer points than the Binary table
- **Profile / step:** `fx5u-eth-3e-ascii` / G1-03, G1-04.
- **Spec says:** Â§4.4 3E limit: 960 words per batch word access, for both data codes.
- **PLC does:** a 960-word write gets no answer: `CAP-fx5u-eth-3e-ascii-G1-03` (`WriteWords D100 x960`) outcome `timeout`, `waited_ms: 5001.423`. A 960-word read answers `plcError C052` (info `00/FF/03FF/00/0401/0000`): `G1-03.2`, `G1-04`. `fx5u-eth-3e-bin` accepts the same 960 words (`G1-03`, `G1-03.2`, `G1-04`: `ok`). FX5 manual, command table: "ASCII: 480 words (7680 points)" and "ASCII: 1792 points" against "BIN: 3584 points" (`fx5-eth.txt:1776-1803`, `2057-2073`).
- **Replay effect:** the captures carry no `frame.max*` keys (caps 0), so the codec does not split at 480 and RPL-02/RPL-03 differ.
- **Status:** decided
- **Decision (owner, 2026-10-14):** four optional `FrameConfig` caps (`maxReadWords`, `maxWriteWords`, `maxReadBits`, `maxWriteBits`); suggested FX5 ASCII values 480 / 480 / 1792 / 1792 (T-088).

### F-003 - FX3U-ENET-ADP over 1E accepts fewer points than the spec table
- **Profile / step:** `fx3-eth-1e-bin`, `fx3-eth-1e-ascii` / G1-03, G1-04, G2-04; `fx3-eth-1e-bin` / G8-Q7-W.
- **Spec says:** Â§4.4 1E limits from the A-series PDF Appendix 5 (Â§10.1 Q7).
- **PLC does:** end code `57H` for 256 words and for a 256-bit write: `CAP-fx3-eth-1e-bin-G1-03` (`WriteWords D100 x256`), `G1-03.2` and `G1-04` (`ReadWords D100 x256`): `plcError 0057`; `G2-04` (`WriteBits M100 x256`, request `02 FF 0A 00 64 00 00 00 20 4D 00 00 10 10 ...`): `plcError 0057`, while the 256-bit read `G2-04.2` is `ok`. The same in `fx3-eth-1e-ascii` (`G1-03`, `G1-04`, `G2-04`: `0057`). `G8-Q7-W` (`WriteWords M112 x41`, a bit device, mutated): `peerClosed` after 10.609 ms (2026-10-15 re-capture), no answer. Manual: command table "256 points / 32 words (512 points) / 160 points / 10 words (160 points)" (`fx3-enet.txt:4631-4649`, `4694-4712`). Counts between the manual limit and 255 were not probed; `G8-Q7` and `G8-Q7-R` were skipped ("scratch too small").
- **Replay effect:** caps 0 in the captures; RPL-02 reports the recorded verdict `unsupported`, and the mock (which does not enforce caps) accepts what the PLC rejected, so RPL-03 differs (also `G2-04.2`: the mock applied the 256-bit write).
- **Status:** decided
- **Decision (owner, 2026-10-14):** per-target caps as in F-002; suggested FX3U-ENET-ADP over 1E 32 / 10 / 256 / 160 (T-088). Mock enforcement of the caps is a follow-up.

### F-004 - F3 short responses carry no SUM (Â§10.1 Q1)
- **Profile / step:** `q03ude-c24-3c-f3` / G8-Q1, G8-Q1.2.
- **Spec says:** `QACK` and `QNAK err` are printed without a sum check; `f3ShortResponseHasSum` default `false`.
- **PLC does:** with `f3ShortResponseHasSum=false` (override in the capture) the write is answered `CAP-q03ude-c24-3c-f3-G8-Q1-R`: `02 46 39 30 30 30 30 46 46 30 30 51 41 43 4B 03` = STX, `F9`, `0000`, `FF`, `00`, `QACK`, ETX, nothing after ETX. The error case `G8-Q1.2-R` (read of D12288): `02 46 39 30 30 30 30 46 46 30 30 51 4E 41 4B 34 30 33 31 03` = `QNAK 4031 ETX`, also without sum. Both parsed (`verdict: passed`).
- **Status:** decided - keep
- **Decision (owner, 2026-10-14, T-089 Plan):** keep: the PLC agrees with the printed spec and the default `false`.

### F-005 - F2 response echoes the request's block number (Â§10.1 Q2)
- **Profile / step:** `q03ude-c24-3c-f2` / G8-Q2, G8-Q2-M.
- **Spec says:** the Block No. of the response SHOULD equal the request's; the library checks it, with an option to disable.
- **PLC does:** request block `5A` (`CAP-q03ude-c24-3c-f2-G8-Q2`: `05 35 41 ...`) is answered `02 35 41 ...` (`G8-Q2-R`). The mutated request with block `5B` (`G8-Q2-M`, override `blockNo=91`: `05 35 42 ...`) is answered `02 35 42 ...` (`G8-Q2-M-R`). The block number is echoed in both cases. (The RPL-02 difference on `G8-Q2-M` is a replay-checker issue: F-018.)
- **Status:** decided - keep
- **Decision (owner, 2026-10-14, T-089 Plan):** keep the check.

### F-006 - Binary odd bit write: a non-zero padding nibble (Â§10.1 Q4)
- **Profile / step:** `q03ude-eth-3e-bin`, `fx5u-eth-3e-bin`, `fx3-eth-1e-bin` / G8-Q4 (G8-Q4.2 reads back), G2-03.
- **Spec says:** the low nibble of the last byte is set to 0 (`part3:504`); whether the PLC ignores a non-zero value was open.
- **PLC does:** the library's own odd writes (padding 0) are accepted by all three (`G2-03.2`, M100 x5, data `10 10 10`; read-back `G2-03.3` = `1 0 1 0 1 0`). With the padding nibble set to 1 (mutated last data byte `11`):
  - Q03UDE: answered `ok`; the read-back `CAP-q03ude-eth-3e-bin-G8-Q4.2-R` (`D0 00 00 FF FF 03 00 05 00 00 00 10 10 10`) shows M105 = 0, so the nibble is ignored.
  - FX5U (`fx5u-eth-3e-bin-G8-Q4`) and FX3U-ENET-ADP (`fx3-eth-1e-bin-G8-Q4`, `02 FF 0A 00 64 00 00 00 20 4D 05 00 10 10 11`): no answer, `peerClosed` after 2.328 ms and 6.699 ms (2026-10-15 re-capture); the follow-up read fails with `LinkDown` (F-012).
  The mock answers `ok` in all cases, so RPL-03 differs on `G8-Q4` of the FX5U and FX3 profiles.
- **Status:** decided - keep
- **Decision (owner, 2026-10-14, T-089 Plan):** keep: the codec writes 0, which all three PLCs accept. Recorded for the owner: the FX5U and the FX3U-ENET-ADP do not ignore a non-zero nibble. The mock-side difference is tagged here and in F-012. Owner, 2026-10-14: stays `decided - keep`; the FX5U and the FX3U-ENET-ADP close the connection on a non-zero padding nibble while the Q03UDE ignores it, and the library always sends 0.

### F-007 - 3E normal response route equals the request route (Â§10.1 Q5)
- **Profile / step:** `q03ude-eth-3e-bin`, `q03ude-eth-3e-ascii`, `fx5u-eth-3e-bin`, `fx5u-eth-3e-ascii` / every normal response; G8-Q5.
- **Spec says:** the manual does not promise it; `checkRoute = false` by default.
- **PLC does:** all 312 normal (`ok`) responses of the four 3E profiles carry the request's route (network 00, PC FF, I/O 03FF, station 00): e.g. `CAP-q03ude-eth-3e-bin-GV-01-R` begins `D0 00 00 FF FF 03 00`, answering `50 00 00 FF FF 03 00`. For a non-default route (`G8-Q5`, `station` 01) the Q03UDE answers an error, `plcError C05F`, with the route in the error information `00/FF/03FF/01/0401/0000` (both Q03UDE profiles). The FX5U captures of G8-Q5 are `transportError LinkDown` (F-012), so there is no non-default answer from it. The mock answers `ok` to the station-01 request (F-009).
- **Status:** decided - keep
- **Decision (owner, 2026-10-14, T-089 Plan):** keep `checkRoute = false`.

### F-008 - Mock fidelity: the error code differs (out of range, wrong sum)
- **Profile / step:** every Q03UDE and FX5U profile / G5-02, G5-03, G5-04; `fx3-eth-1e-*` / G5-04; the four `q03ude-c24-3c-*` profiles with a sum, `fx5u-rs485-3c-f1-sum`, `fx3-serial-1c-f1-sum` and `fx3-serial-1c-f4` / G7-03; `q03ude-c24-3c-f3` / G8-Q1 (G8-Q1.2).
- **Spec says:** RPL-03: the mock answers exactly like the PLC.
- **PLC does / mock does:** out-of-range device numbers: the Q03UDE over 3E and 3C answers `4031` (`CAP-q03ude-eth-3e-bin-G5-02`, D12288; `G5-03`, D12287 x2; `G5-04`, M8192), the mock `C051`; the FX5U answers `C056` (`CAP-fx5u-eth-3e-bin-G5-02/03/04`), the mock `C051`; the FX3 answers end code `58H` for M7680 (`CAP-fx3-eth-1e-bin-G5-04`), the mock `5BH`. A 3C request with altered sum characters (`G7-03`) is answered `NAK 7F24` by the C24; the mock sends another code (first difference at byte 12 of the NAK: captured `46`, mock `31`). The FX5U RS-485 gives `7143` for the out-of-range numbers (`CAP-fx5u-rs485-3c-f1-sum-G5-02`, `G5-03`, `G5-04`; first difference at byte 11: captured `37`, mock `43`) and `7F24` for the altered sum (`G7-03`, the same byte-12 difference as the C24). The FX3 over 1C answers the altered sum with `NAK 02` (`CAP-fx3-serial-1c-f1-sum-G7-03-R`: `15 30 30 46 46 30 32`), the mock with `06` (first difference at byte 6: captured `32`, mock `36`); the same in `fx3-serial-1c-f4`. The FX3 over 1C answers M7680 with `NAK 06` (`G5-04`), as the mock does.
- **Mock now:** with `MockOptions::target` set, the mock answers the captured codes: `QCpu` 4031 (3E and 3C, out of range) and 7F24 (3C, altered sum); `Fx5` C056 (3E) and 7143 (3C) for out-of-range numbers and 7F24 (3C) for an altered sum; `Fx3` end code `58H` without an abnormal code (1E, out of range) and `NAK 02` for an altered sum over 1C (formats 1 and 4). The FX3 over 1C keeps `NAK 06` for M7680, as before. The default target `Generic` keeps the `MockOptions` codes (`C051`, `5BH` + `10H`, `7151`, `06`). `makeMock` in the replay maps `plc` Q03UDECPU to `QCpu`, FX5U-... to `Fx5`, FX3U-... to `Fx3`, anything else to `Generic`.
- **Status:** decided
- **Decision (owner, 2026-10-21):** the mock answers the code of the PLC it imitates. `MockOptions::target` (`Generic`, `QCpu`, `Fx5`, `Fx3`) chooses the code table; the replay sets it from `run.meta` `plc`. The `F-008` tags are removed.

### F-009 - Mock fidelity: other-station route, iQ-R subcommand, random read and chained frames on the Q03UDE
- **Profile / step:** `q03ude-eth-3e-bin`, `q03ude-eth-3e-ascii` / G8-Q5, G9-01, G9-05, G9-05-M, G9-06; `q03ude-c24-3c-f1-sum` / G9-01; `-f2`, `-f3`, `-f4` / G9-01; all five `q03ude-c24-3c-*` / G9-05, G9-05-M; `fx5u-rs485-3c-f1-sum` / G9-01-F1, G9-05, G9-05-M.
- **Spec says:** RPL-03: the mock answers like the PLC.
- **PLC does / mock does:**
  - Station 01 (G8-Q5): PLC `C05F`, mock `ok`.
  - iQ-R subcommand `0002` / `0003` on a Q (G9-05, G9-05-M): over 3E the PLC answers `plcError C059` with info `.../0401/0002` and `.../0401/0003`, the mock `ok`; over 3C the C24 answers `NAK 7F22`, the mock a data frame; the FX5U RS-485 answers `NAK 7E40` (`CAP-fx5u-rs485-3c-f1-sum-G9-05-R`, `G9-05-M-R`), the mock a data frame.
  - Random read 0403 (G9-01): the Q03UDE over 3E answers 21 bytes (`q03ude-eth-3e-bin-G9-01-B-R`, length field `0C 00`), the mock 20 (length `0B`). Over 3C F2 / F3 / F4 the C24 answers with data (36 / 38 / 36 bytes), the mock with a NAK; over 3C F1 with sum the C24 does not answer (`timeout`) and the mock sends a NAK. The FX5U RS-485 (3C F1 with sum) answers the random read with data (34 bytes, `CAP-fx5u-rs485-3c-f1-sum-G9-01-F1-R`), the mock with a NAK (15 bytes).
  - Two requests in one segment (G9-06): the PLC answers once (13 bytes), the mock twice (26).
- **Status:** decided - keep
- **Decision (owner, 2026-10-21):** keep. The mock does not model the other-station route, the iQ-R subcommand on a Q-series CPU, the random read or chained frames (mock 1.0 limit: X, Y, M, D); the tags stay.

### F-010 - Mock fidelity: `MockPlc` ignores the profile's `supports` list and special device ranges
- **Profile / step:** `q03ude-*` (3E and 3C) / G4-02-STS, -STC, -STN, -R, -ZR; `fx5u-eth-3e-*` / G4-02-V, -DX, -DY, -ZR; `fx3-eth-1e-*` and the three `fx3-serial-1c-*` profiles / G4-02-F, -B, -W, -TC, -CC (the 1C profiles also G4-02-L) and G0-02, G1-09, G2-10, G3-04, G5-02; `fx5u-rs485-3c-f1-sum` / G4-02-V, -DX, -DY, -ZR.
- **Spec says:** SPEC-hil-capture: the sweep "also probes the family's other types and expects a PLC error"; `specialFrom` names a special range beyond `deviceEnd` that exists.
- **PLC does / mock does:**
  - Types outside `supports`: the Q03UDE answers `4031` (3E `plcError 4031`, 3C NAK 4031; `CAP-q03ude-eth-3e-bin-G4-02-R`, `-ZR`, `-STS`, `-STC`, `-STN`), the FX5U `C05C` (V, ZR; DX, DY in the Binary profile) or `C050` (DX, DY in the ASCII profile), the FX3 end code `56H` over 1E (`CAP-fx3-eth-1e-bin-G4-02-F`, `-B`, `-W`, `-TC`, `-CC`) and `NAK 06` over 1C (`CAP-fx3-serial-1c-f1-sum-G4-02-L-R`, `-F`, `-B`, `-W`, `-TC`, `-CC`), the FX5U RS-485 `NAK 7E43` (V, ZR) or `NAK 7F21` (DX, DY). The mock answers `ok` with data.
  - FX3 special range (`D=7999` in `device_end`, special from D8000 / M8000): the PLC answers `ok` for `D8010` (G0-02), `D8000` x4 (G1-09), `M8000` x16 (G2-10), `M8000` as word (G3-04) and `D8000` (G5-02, the plan expects `ok`); the mock answers `5BH` because those numbers lie beyond `deviceEnd`. Over 1C the three serial profiles show the same: the PLC answers with data (`CAP-fx3-serial-1c-f1-nosum-G0-02-R`: `02 30 30 46 46 30 30 30 35 03`), the mock with a NAK.
- **Mock now:** `MockPlc::setTypeSupported()` refuses a device type with the type-not-supported code of the mock's target (`QCpu` 4031, `Fx5` C05C / C050 over 3E and 7E43 / 7F21 over 3C, `Fx3` end code `56H` over 1E and `NAK 06` over 1C; `Generic` C059). The profile's `supports` lists only the types the capture plan tests, and the Q03UDE and the FX5U also answer other types with data (e.g. `CAP-q03ude-eth-3e-bin-G4-02-V-R`, `CAP-fx5u-eth-3e-bin-G4-02-Z-R`), so the replay refuses, for `plc` Q03UDECPU, exactly the types its captures show refused (STS, STC, STN, R, ZR) and, for FX5U-..., V, DX, DY and ZR. For `plc` FX3U-... (and any other `plc`) the replay refuses every type outside `supports`, and nothing when the key is missing or empty. For FX3U-... it also sets the special range `[8000, 8512)` for the type of `special_word` (D) and of `special_bit` (M) with `MockPlc::setSpecialRange()`, because their number lies beyond `device_end`: the mock then answers `ok` for D8010, D8000 x4, M8000 x16, M8000 as a word and D8000, and still refuses a request that leaves both ranges (`58H`). The FX3 manuals give D8000 to D8511 and M8000 to M8511 (`fx3-enet.txt:4744`, `fx3-enet.txt:4771`, `fx3-dc.txt:17139`, `fx3-dc.txt:17241`).
- **Status:** decided
- **Decision (owner, 2026-10-21):** the mock honours both: `MockPlc::setTypeSupported()` for the types the PLC refuses (the replay takes them from `supports` for the FX3 and from the captured refusals for the Q03UDE and the FX5U) and `MockPlc::setSpecialRange()` for the FX3 special devices. The special range ends at 8511 (D8000 to D8511, M8000 to M8511, as the manual says). The `F-010` tags are removed.

### F-011 - Mock fidelity: the FX3U-ENET-ADP answers an unknown subheader with `FF 50`
- **Profile / step:** `fx3-eth-1e-bin` / G7-02-B, `fx3-eth-1e-ascii` / G7-02-A.
- **Spec says:** (no rule; the mock stays silent for a frame it does not decode.)
- **PLC does:** a request with the subheader changed to `7F` gets a two-byte answer `FF 50` (Binary, `CAP-fx3-eth-1e-bin-G7-02-B-R`) or `46 46 35 30` = "FF50" (ASCII, `CAP-fx3-eth-1e-ascii-G7-02-A-R`); the library reports `protocolError FrameMismatch`. The mock sends nothing.
- **Status:** decided - keep
- **Decision (owner, 2026-10-21):** keep. The mock stays silent for a frame it does not decode (PLC behaviour of the FX3U-ENET-ADP, recorded here); the tags stay.

### F-012 - Tool: the next step is sent before the link is up again after a peer close
- **Profile / step:** `fx3-eth-1e-ascii` / G4-03, G5-01, G8-Q3 (G8-Q3.2); `fx3-eth-1e-bin` / G4-03, G5-01, G7-01.2, G8-Q4.2; `fx5u-eth-3e-bin` / G8-Q4.2, G8-Q5; `fx5u-eth-3e-ascii` / G8-Q5.
- **Spec says:** "On `LinkFault` the tool reconnects (`connectToPlc()`), records the recovery, and continues with the next step" (SPEC-hil-capture, capture tool).
- **PLC does / tool does:** after a step that ended `peerClosed` (the PLC closed the connection), the next step is sent at once and recorded `transportError LinkDown`, `waited_ms` 2.3 to 11.7 (e.g. `CAP-fx3-eth-1e-bin-G8-Q4.2`, `CAP-fx5u-eth-3e-bin-G8-Q5`); the reconnect (`run.meta` `recovery.N`) follows after that step. Such a step says nothing about the PLC (in `G8-Q4.2` the read-back of the Q4 test is lost). In the 2026-10-15 FX3 re-captures the same symptom appears, mostly without a `peerClosed` step before it (only G8-Q4.2 follows one, G8-Q4): `fx3-eth-1e-ascii` G4-03 (`LinkDown` after 6.493 ms, following five `0056` answers), G5-01 (4.330 ms, `verdict: failed`; `run.meta` `recovery.1` after G5-01) and G8-Q3 (11.730 ms, `verdict: failed`) with G8-Q3.2 (10.855 ms), sent after the recovery that followed G7-02-A; `fx3-eth-1e-bin` G4-03 (6.329 ms, after five `0056` answers), G5-01 (2.255 ms, `verdict: failed`), G7-01.2 (8.172 ms, `verdict: failed`; G7-01 ended `timeout` after 3501.719 ms and `run.meta` records a `Requested` and a `PeerClosed` recovery after it) and G8-Q4.2 (10.807 ms). The capture does not show what closed the link in these cases; a failed send is the only sign. The mock answers these steps, so RPL-03 differs.
- **Status:** open
- **Decision (owner, date):** open. The fix is in `hil_capture`: wait for the link before the next step, or repeat the step after the reconnect.

### F-013 - PLC behaviour: FX3U-ENET-ADP and FX5U close the TCP connection after some requests; the mock answers
- **Profile / step:** `fx5u-eth-3e-bin` / G8-Q4, G9-01-B, G9-05, G9-05-M, G9-06; `fx5u-eth-3e-ascii` / G9-01-A, G9-05, G9-05-M, G9-06; `fx3-eth-1e-bin` / G8-Q4, G8-Q7-W (and G7-01, whose truncated request ends `timeout` with the link closed afterwards); `fx3-eth-1e-ascii` / G7-01 and the `run.meta` recoveries. Capture ids: `CAP-fx5u-eth-3e-bin-G9-05`, `CAP-fx5u-eth-3e-ascii-G9-05`, `CAP-fx3-eth-1e-bin-G8-Q4`.
- **Spec says:** (nothing; the mock never closes a connection on a request it understands.)
- **PLC does:** after these requests the PLC sends no response and closes the connection: outcome `peerClosed` (`waited_ms` 2.0 to 10.6) and a `PeerClosed` recovery in `run.meta`. `PeerClosed` recoveries: 9 of 11 in `fx5u-eth-3e-bin`, 6 of 8 in `fx5u-eth-3e-ascii`, 7 of 9 in `fx3-eth-1e-bin`, 5 of 6 in `fx3-eth-1e-ascii` (2026-10-15 re-captures), none in the Q03UDE profiles (they answer the same probes with an error, F-009) and none in the serial profiles (no connection to close). The FX3U-ENET-ADP manual lists closes on the existence-confirmation timeout, on RST / close, and when the external device does not acknowledge (`fx3-enet.txt:3474-3528`); a close after a rejected request is not listed in the dumps, so the cause is not stated.
- **Replay effect:** RPL-03 differs where the mock answers (G8-Q4, G9-* on the FX5U).
- **Status:** decided - keep
- **Decision (owner, 2026-10-21):** keep. The mock never closes a connection on a request it understands (PLC behaviour of the FX3U-ENET-ADP and the FX5U, recorded here); the tags stay.

### F-014 - withdrawn 2026-10-14 (owner): not recorded. The id is not reused.

### F-015 - Tool: the RPL-05 read-back check cannot follow the plan's `then` chains
- **Profile / step:** every profile / GV-01, GV-03, G2-01; the three `fx3-serial-1c-*` profiles / also G3-05 (G3-05.2); `q03ude-c24-3c-f3` / G8-Q1.
- **Spec says:** RPL-05 (a): "every `write` step is followed by a read-back that matched".
- **Checker does:** `tests/hil/replay/replay.cpp` `checkSanity` (a) accepts only reads of the same base step (`rd.step != w.step` skips) and keeps the last value read. `GV-01` (write D100 x3) and `GV-03` (write M100 x8) are read back by the next steps `GV-02` / `GV-04.2`, so the same-step search finds nothing ("the write of D100 x3 has no read-back ... (the plan needs readBack)"). `G2-01` writes 1, reads, writes 0, reads: the checker compares the first write (1) with the last read (0): "the read-back of M100 is 0 but 1 was written". `G8-Q1` writes without a read in the step. `G3-05` (`fx3-serial-1c-*`, a bit device accessed as words) reads M112 x32 and then writes M112 x10 in a `then` step with no read-back: "the write of M112 x10 has no read-back of M112 (the plan needs readBack)". The PLCs read back correctly (`CAP-q03ude-eth-3e-bin-G2-01.2-R` bit 1, `G2-01.4-R` bit 0).
- **Checker now:** the read-back follows the capture order: per point the latest successful write is kept, every later read of the point must return it (a mismatch fails the write's step), and a point written again before any read needs no read-back. `GV-01`, `GV-03` and `G2-01` pass in every profile. `G3-05.2` (`fx3-serial-1c-*`) and `G8-Q1` (`q03ude-c24-3c-f3`) write points that no later read of the capture returns, so they are still reported and stay tagged.
- **Status:** decided
- **Decision (owner, 2026-10-20):** fix the checker to follow the chain of writes and reads. The `GV-01`, `GV-03` and `G2-01` tags are removed; the `G3-05` and `G8-Q1` tags stay because their writes have no read-back in the capture.

### F-016 - Tool: RPL-05 compares the request with the Appendix A vector even when the profile differs
- **Profile / step:** `q03ude-c24-3c-f1-nosum` / GV-02, GV-03; `q03ude-c24-3c-f2` / GV-02, GV-03; `fx3-serial-1c-f1-nosum` / GV-01, GV-02, GV-03, GV-04. Capture ids: `CAP-q03ude-c24-3c-f1-nosum-GV-02`, `CAP-q03ude-c24-3c-f2-GV-02`, `CAP-fx3-serial-1c-f1-nosum-GV-02`.
- **Spec says:** RPL-05 (b): "every GV step's bytes equal the Appendix A vector it mirrors (request always; response when the route and values are the defaults)".
- **Checker does:** the vectors `V-3C1-01/03` carry a sum check and `V-3C2-01/03` the block number `00`. The profile `f1-nosum` has no sum (the capture request is 31 bytes against the vector's 33 for GV-02, 39 against 41 for GV-03) and `f2` uses block `5A` (byte 1 `35` against `30`). The request differs from the vector for a documented reason, and the check reports it. The 1C vectors `V-1C1-01/03/05/06` carry a sum check too: in `fx3-serial-1c-f1-nosum` GV-01 is 27 bytes against 29, GV-02 15 against 17, GV-03 23 against 25 and GV-04.2 15 against 17. `f1-sum`, the 3E profiles, `fx3-serial-1c-f1-sum`, `fx3-serial-1c-f4` and `fx5u-rs485-3c-f1-sum` equal the vectors.
- **Checker now:** when the profile's `sumCheck` and `blockNo` equal the vector's (sum check on, block `00`) the bytes are compared as before. Otherwise the request is compared with the vector re-encoded with the profile's `FrameConfig`, and the response by its decoded content (parse outcome and payload values) instead of by bytes. A changed command or device byte still fails. The `GV` tags of these profiles are gone.
- **Status:** decided
- **Decision (owner, 2026-10-20):** normalize the vector for the profile, then compare. The `F-016` tags are removed.

### F-017 - Tool: the EOT step outcome is recorded `ok` while the PLC answered a NAK
- **Profile / step:** the five `q03ude-c24-3c-*` profiles / G7-01+2.
- **Spec says:** (G7-01: a truncated request, then EOT, then a normal request.)
- **Tool does / PLC does:** the EOT (`04`) after the truncated request (`CAP-q03ude-c24-3c-f1-sum-G7-01+2`) is answered `15 46 39 30 30 30 30 46 46 30 30 37 46 32 34` = NAK, `F9`, `0000`, `FF`, `00`, `7F24` (`f1-nosum` answers `7F22`, `f4` `7F23`, `f3` the same as a `QNAK 7F24` frame). The step outcome is `ok` with no `expect`. The parser reads the same bytes as `PlcError`, so RPL-02 reports "recorded outcome ok, the parser says failed with PlcError". The mock answers nothing to a lone EOT (RPL-03: captured 15 to 20 bytes, mock 0).
- **Status:** open
- **Decision (owner, date):** open.

### F-018 - Tool: RPL-02 parses a mutated step with the profile's FrameConfig, not the step's override
- **Profile / step:** `q03ude-c24-3c-f2` / G8-Q2-M. Capture id: `CAP-q03ude-c24-3c-f2-G8-Q2-M`.
- **Spec says:** RPL-02: "A step that ran with a frame override is parsed with the profile's `FrameConfig` with that override applied."
- **Checker does:** `G8-Q2-M` ran with `override: blockNo=91` (0x5B) and the PLC echoed `5B` (F-005). RPL-02 parsed it with the profile's block `5A` and reported "recorded outcome ok, the parser says failed with FrameMismatch".
- **Checker now:** a record with `override:` is parsed (and its seeding for the mock read) with a copy of the profile's `FrameConfig` with the override applied; a relative value such as `stationNo=+1` adds to the profile's number, and an override key the replay does not know is a failure. RPL-01 still skips such steps. `G8-Q2-M` passes.
- **Status:** decided
- **Decision (owner, 2026-10-20):** parse with the profile's config plus the step's override. The `G8-Q2-M` tag is removed.

### F-019 - Tool: RPL-04 session replay differs by 3 ms on one poll round
- **Profile / step:** `q03ude-c24-3c-f3` / G7-06. Capture id: the poll step `G7-06` in `session.vec` of `q03ude-c24-3c-f3`.
- **Spec says:** RPL-04: the replayed events match the recorded ones.
- **Checker does:** "event #9456: cycleDone round 3152: duration 41 ms captured, 38 ms replayed". The captured chunk times are in nanoseconds; the capture records 41 ms, the fake-clock replay 38 ms. A 1 ms rounding was the first guess, but the difference is 3 ms, so the cause is not established. All other rounds of this and the other poll steps match.
- **Checker now:** a poll round's duration may differ from the recorded one by up to 5 ms (the stamps and the Session clock are rounded from different origins); 6 ms and more still fails. The 3 ms round passes.
- **Status:** decided
- **Decision (owner, 2026-10-20):** allow a tolerance of 5 ms. The `G7-06` tag is removed.

### F-020 - Tool: RPL-03 does not model the PLC dropping an incomplete frame; after F-001 the mock swallows the following requests
- **Profile / step:** `fx3-eth-1e-ascii` / G1-01, G2-02, G2-06, G2-08, G3-01, G3-02, G4-01-D, -R, -M, -X, -Y, -TS, -TN, -CS, -CN of the 2026-10-14 capture (steps that failed only since T-087). No step of the current capture is tagged.
- **Spec says:** RPL-03: the steps are replayed in order into a `MockPlc`.
- **Checker does / PLC does:** since T-087 the mock waits for the dummy of an odd bit write (MCK-14). In the capture the PLC timed out after 3.5 s on `GV-05`, dropped the partial frame and answered the next request (`GV-05.2-R` is a normal read answer). The replay has no clock: the mock keeps waiting and takes the characters of the following requests as the missing data. From `GV-05.2` on, the mock's answers belong to earlier requests (e.g. `G1-01`: captured `83 00`..., mock a read answer `80 ...`). The steps above pass in `build/_scratch-leader/replay-all.log` (before T-087) and fail only now. Re-capturing the profile (F-001) removes the cause; dropping the partial frame at a captured timeout in the harness removes it for old captures.
- **Re-capture (2026-10-15 capture imported):** in the re-captured `fx3-eth-1e-ascii` none of the steps above fails in the no-tags replay (G1-01, G2-02, G2-06, G2-08, G3-01, G3-02 and G4-01-* all pass): the re-capture sends the dummy, so no partial frame is left on the stream. The harness limitation stays for any future capture in which the PLC times out on a partial frame.
- **Status:** decided
- **Decision (owner, 2026-10-20):** closed by the owner: the re-capture removed its steps; the harness limitation is kept as a note above. Earlier: owner, 2026-10-14, the `fx3-eth-1e-ascii` capture was committed as it was and replaced after a re-capture with the T-087 codec; the re-capture of 2026-10-15 is imported and its tags are gone.

### F-021 - Bench note: Y0-Y7 of the Q03UDE are the QJ71C24N's own I/O
- **Profile / step:** all `q03ude-*` profiles (scratch in `run.meta`).
- **Spec says:** the first Q03UDE profile draft listed `Y0-Y7` as scratch.
- **PLC does:** the module's own I/O occupies Y0-Y7 on this bench (decided 2026-10-12), so the captures of 2026-10-13/14 declare `scratch: ... Y10-Y17`. Earlier captures were not imported. No step is tagged.
- **Status:** decided
- **Decision (owner, 2026-10-21):** closed. The `Y10-Y17` captures stand in for `Y0-Y7`; no re-capture.

### F-022 - Mock fidelity: the FX3 rejects the AnA bit-read command over 1C
- **Profile / step:** `fx3-serial-1c-f1-nosum`, `fx3-serial-1c-f1-sum`, `fx3-serial-1c-f4` / G4-04-M, -S, -X, -Y, -TS, -CS. Capture ids: `CAP-fx3-serial-1c-f1-nosum-G4-04-M`, `CAP-fx3-serial-1c-f1-sum-G4-04-M`, `CAP-fx3-serial-1c-f4-G4-04-M`.
- **Spec says:** the plan step G4-04 repeats the supported reads "with the AnA command set" and records the answer (`expect: record`); RPL-03: the mock answers like the PLC.
- **PLC does / mock does:** with `commandSet=AnA` the library reads word devices with `QR` and bit devices with `JR`. The FX3 answers the word reads (G4-04-D, -R, -TN, -CN: `ok`, data) and rejects every bit read with a NAK: `CAP-fx3-serial-1c-f1-nosum-G4-04-M` (`05 30 30 46 46 4A 52 30 4D 30 30 30 30 30 30 30 31`, i.e. `00FFJR0M00000001`) is answered `15 30 30 46 46 30 33` (NAK, `00`, `FF`, code `03`); `f4` the same code followed by CR LF; `f1-sum` (request with the sum `86`) answers code `02`. The sum in that request is correct (low byte of the character sum, recomputed), so `02` there is not explained by the dump. The computer-link command table of the manual lists `BR`, `WR`, `QR`, `BW`, `WW`, `QW`, `BT`, `WT`, `QT`, `RR`, `RS` and further commands, and no `JR` (`fx3-dc.txt:20350-20400`; the dump holds no `JR` at all). The mock used to answer the bit reads with data (RPL-03: captured NAK, mock `02`); since the decision below, a mock configured as an FX3 computer link (1C, `targetFamily A`, `xyNotation Octal`) does not execute `JR` and answers NAK `02` in format 1 with the sum check on and `03` in every other case (it keys on the request letters, not on `commandSet`; `QR`, `JW` and `QW` are executed). The `02` with a correct sum stays unexplained by the dump; the mock reproduces the captured behaviour.
- **Status:** decided
- **Decision (owner, 2026-10-20):** the mock rejects `JR` for the FX3 (target family A, octal X/Y) with the FX3's codes: `02` in format 1 with the sum check on, `03` in every other case. The `G4-04-*` tags of the three `fx3-serial-1c-*` profiles are removed.

### F-023 - PLC behaviour: the FX5U RS-485 answers a bit access of Y10 x8 with 7F21
- **Profile / step:** `fx5u-rs485-3c-f1-sum` / G2-06 (G2-06.2). Capture ids: `CAP-fx5u-rs485-3c-f1-sum-G2-06`, `CAP-fx5u-rs485-3c-f1-sum-G2-06.2`.
- **Spec says:** the plan step G2-06 writes eight bits of the scratch Y range with a read-back and expects `ok`. SPEC-hil-capture, "X/Y numbering on FX5 and FX3": FX X/Y numbers are octal; `xyNotation = Octal` makes the library read and write them that way, `xyAsciiDigits` picks the digits an ASCII frame carries (FX5 `ASCII (X,Y HEX)`: the point index in hex; FX5 `ASCII (X,Y OCT)`: octal digits); a capture (G2, G6) that disagrees is a finding. The profile sets `frame.xyNotation: Octal` and `frame.xyAsciiDigits: Hex`, so Y10 goes out as `Y*000008`.
- **PLC does / mock does:** `G2-06` (`WriteBits Y10 x8`, head `Y*000008`, data `10101010`) is answered `15 46 39 30 30 30 30 46 46 30 30 37 46 32 31` (NAK `F9`, `0000FF00`, `7F21`), recorded `plcError 7F21`, verdict `unsupported`; the read `G2-06.2` (`ReadBits Y10 x8`) gets the same NAK. Other bit accesses of the profile are answered: `G4-01-Y` (Y0 x1), `G2-08` (X0 x16 and x64), M100 x16 (`G2-02`), B10 x16 (`G2-05`). The same step is accepted by `fx5u-eth-3e-ascii` (Y0 x8) and by `q03ude-c24-3c-f1-sum` (Y10 x8 with the Q-series number `Y*000010`). The capture does not separate the possible causes: the X/Y digit setting of the port (if it is set to `ASCII (X,Y OCT)`, `8` is not an octal digit and the head device `000008` is invalid), a non-zero head device, or eight points at once. `fx5-serial.txt:5225` puts "Data count error" beside 7F21H, but the same table pairs 7F24H with "Device error" (line 5228) where another table says "Sum check error" (line 5359), so the pairing is not reliable. The mock answers `ok`.
- **Status:** decided
- **Decision (owner, 2026-10-20):** the port is set to `ASCII (X,Y OCT)`; the profile sent hex digits (`xyAsciiDigits: Hex`), so Y10 went out as `000008`, which is not an octal number. The profile is not re-captured, and its tag stays (the capture is kept as it is).

### F-024 - Mock fidelity: the FX5U RS-485 answers an incomplete frame with a timeout NAK
- **Profile / step:** `fx5u-rs485-3c-f1-sum` / G7-01 (G7-01+2). Capture id: `CAP-fx5u-rs485-3c-f1-sum-G7-01`.
- **Spec says:** G7-01 sends a request truncated by one character, then EOT, then a normal request; `expect: record`. RPL-03: the mock answers like the PLC.
- **PLC does / mock does:** the truncated request (`05 46 39 30 30 30 30 46 46 30 30 30 34 30 31 30 30 30 30 44 2A 30 30 30 31 30 30 30 30 30 31 30`, the sum is one character) is answered after about 13 ms (`ttfb_ms: 13.438`, `rx_ms: 16.034`) with `15 46 39 30 30 30 30 46 46 30 30 37 46 34 30` = NAK `7F40` (`CAP-fx5u-rs485-3c-f1-sum-G7-01-R`, recorded `plcError 7F40`). FX5 serial manual, 3C/4C error codes: "7F40H Timeout error: a timeout occurred because the received message was insufficient" (`fx5-serial.txt:5371-5372`). The following EOT (`G7-01+2`) gets no answer (31.337 ms); the normal request after it is answered. The Q03UDE over 3C (`CAP-q03ude-c24-3c-f1-sum-G7-01`: `timeout` after 3000.932 ms) and the FX3 over 1C (G7-01 `timeout` after 3001.485 / 3001.964 / 3001.619 ms in `fx3-serial-1c-f1-nosum` / `-f1-sum` / `-f4`; the EOT step G7-01+2 is `noResponse`) send nothing. The mock has no clock and answers nothing to an incomplete frame (RPL-03: captured 15 bytes, mock 0).
- **Status:** decided - keep
- **Decision (owner, 2026-10-21):** keep. The mock has no clock and answers nothing to an incomplete frame (PLC behaviour of the FX5U RS-485, recorded here); the tag stays.

## PLC error codes seen

Built from every `plcError` outcome of the 15 imported runs (sec. 10.1 Q6). The reference spec returns the raw code and holds no meaning table, so no code below is already known to it. Meanings are quoted from the manual dumps with a line reference; "meaning not found in the manuals" means the dumps give none (the Q03UDE / QJ71C24N manuals are not among them). The library does not interpret codes. The mock answers the codes of an out-of-range device, an unsupported device type and an altered sum (`4031`, `C056`, `C05C`, `C050`, `7143`, `7E43`, `7F21`, `7F24`, `0058`, `0056` and `0002` over 1C) when its `MockOptions::target` is the PLC that gave them (`QCpu`, `Fx5`, `Fx3`); with the default target it keeps its own test codes.

| Code | Frame / module | Step and request that caused it | Meaning (from manual) |
|---|---|---|---|
| `0050` (end code `50H`; reported as `protocolError FrameMismatch`, not `plcError`) | 1E, FX3U-ENET-ADP | `fx3-eth-1e-bin` G7-02-B, `fx3-eth-1e-ascii` G7-02-A: subheader changed to `7F` | "Codes for command/response type of subheader are not within specifications (00 to 05H, 13 to 16H)" (`fx3-enet.txt:9488`) |
| `0056` | 1E, FX3U-ENET-ADP | G4-02-F, -B, -W, -TC, -CC: read of F0, B0, W0, TC0, CC0 (types the FX3 does not have), both profiles | "Device designation from the external side is incorrect." (`fx3-enet.txt:9501`) |
| `0057` | 1E, FX3U-ENET-ADP | G1-03 (write D100 x256), G1-03.2 and G1-04 (read D100 x256), G2-04 (write M100 x256 bits), both profiles | by the dump text around the `57H` label: the number of points exceeds the maximum per communication; head device plus points exceeds the maximum address; a command byte length that does not conform; a point count in the data that differs from the specified one (`fx3-enet.txt:9503-9522`; the label sits between two bullet groups, so the grouping is by dump order, confirm in the PDF page 160) |
| `0058` | 1E, FX3U-ENET-ADP | G5-04: read M7680 (deviceEnd M7679), both profiles | by the dump text after the `58H` label: head device number outside the allowable range; a word device in a bit command; head number of bit devices not a multiple of 16 in a word command (`fx3-enet.txt:9524-9531`; same caveat) |
| `4031` | 3E (Q03UDE built-in Ethernet), 3C (QJ71C24N, F1 to F4) | G4-02-STS, -STC, -STN, -R, -ZR (types outside `supports`); G5-02 (D12288), G5-03 (D12287 x2), G5-04 (M8192); G8-Q1.2 (D12288) | meaning not found in the manuals (`4031` appears in the FX5 manual only as a web-server CGI result "Out of device range", `fx5-eth.txt:8865`, which is another protocol) |
| `C059` | 3E (Q03UDE) | G9-05 (read D100 with the iQ-R subcommand `0002`), G9-05-M (M100 x8, subcommand `0003`) | meaning not found in the manuals |
| `C05F` | 3E (Q03UDE) | G8-Q5: read D100 with `station` 01 | meaning not found in the manuals |
| `7F22` | 3C (QJ71C24N, all five profiles) | G9-05, G9-05-M (iQ-R series override on a Q); in `f1-nosum` also the answer to the EOT in G7-01+2 | meaning not found in the manuals |
| `7F23` | 3C (QJ71C24N, F4) | the answer to the EOT in G7-01+2 | meaning not found in the manuals |
| `7F24` | 3C (QJ71C24N, F1 sum, F2, F3, F4) and 3C (FX5U RS-485, F1 sum) | G7-03 (altered sum characters); in the QJ71C24N `f1-sum`, `f2`, `f3` also the answer to the EOT in G7-01+2 | not found in the Q-series manuals (not among the dumps); the FX5 serial manual lists 7F24H as "Sum check error": the received sum check code does not match the calculated one (`fx5-serial.txt:5359-5360`), which fits the altered sum of G7-03 |
| `C050` | 3E ASCII (FX5U) | G4-02-DX, G4-02-DY (read DX0 / DY0) | not resolvable from the dump: the table "SLMP function error code" (`fx5-eth.txt:13295-13334`) lists C050H to C061H in one column and the descriptions in another, and the row alignment is lost in the text. Meaning not found in the dump; read the PDF page 272 |
| `C052` | 3E ASCII (FX5U) | G1-03.2, G1-04: read D100 x960 | same dump problem (`fx5-eth.txt:13303-13334`); the context is a point count above the ASCII limit (F-002) |
| `C056` | 3E (FX5U, Binary and ASCII) | G5-02 (D8000), G5-03 (D7999 x2), G5-04 (M7680), numbers beyond `deviceEnd` | same dump problem (`fx5-eth.txt:13303-13334`) |
| `C05C` | 3E (FX5U, Binary and ASCII) | G4-02-V, -ZR (both profiles); G4-02-DX, -DY (Binary profile only): types outside `supports` | same dump problem (`fx5-eth.txt:13303-13334`) |
| `0002` | 1C (FX3U computer link, FX3U-232-BD) | `fx3-serial-1c-f1-sum` G4-04-M, -S, -X, -Y, -TS, -CS (AnA `JR` bit reads) and G7-03 (altered sum characters); `fx3-serial-1c-f4` G7-03 | "Sum check error": the sum check code in the received data differs from the value calculated from it (`fx3-dc.txt:22645-22650`). For G4-04 the request's sum is correct, so the dump does not explain the code there (F-022). The mock answers `02` for `JR` in format 1 with the sum check on when configured as an FX3 |
| `0003` | 1C (FX3U computer link) | `fx3-serial-1c-f1-nosum`, `fx3-serial-1c-f4` G4-04-M, -S, -X, -Y, -TS, -CS (AnA `JR` bit reads) | "Protocol error": the communication protocol is abnormal; among the causes "a command specified in the preset control procedure does not exist" (`fx3-dc.txt:22652-22666`). The mock answers `03` for `JR` in every other case when configured as an FX3 (F-022) |
| `0006` | 1C (FX3U computer link) | all three `fx3-serial-1c-*`: G4-02-L, -F, -B, -W, -TC, -CC (types the FX3 does not have), G5-04 (M7680) | "Character area error": an error in character area A, B or C or a specified command that does not exist; a specified device number that does not exist in the target PLC (`fx3-dc.txt:22668-22684`) |
| `7143` | 3C (FX5U RS-485) | G5-02 (D8000), G5-03 (D7999 x2), G5-04 (M7680): numbers beyond `deviceEnd` | meaning not resolvable from the dump: "Device number error" sits in the 7143H rows (`fx5-serial.txt:5212-5214`, `5309-5310`), but the description column is interleaved with the neighbouring rows; read the PDF |
| `7E40` | 3C (FX5U RS-485) | G9-05 (read D100 with the iQ-R subcommand `0002`), G9-05-M (M100 x8, subcommand `0003`) | meaning not resolvable from the dump: the two tables pair 7E40H with different items ("Command error", `fx5-serial.txt:5220`; "Disabled in RUN mode", line 5313) |
| `7E43` | 3C (FX5U RS-485) | G4-02-V, G4-02-ZR (types outside `supports`) | same dump problem (`fx5-serial.txt:5224`, `5316`); meaning not found |
| `7F21` | 3C (FX5U RS-485) | G2-06 (write Y10 x8), G2-06.2 (read Y10 x8), G4-02-DX, G4-02-DY (types outside `supports`) | same dump problem (`fx5-serial.txt:5225`, `5317`); meaning not found (F-023) |
| `7F40` | 3C (FX5U RS-485) | G7-01: request truncated by one character | "Timeout error": a timeout occurred because the received message was insufficient (`fx5-serial.txt:5371-5372`) (F-024) |
