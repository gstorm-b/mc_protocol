# HIL Findings

- **Belongs to:** `docs/spec/SPEC-hil-capture.md` (decision H3)
- **Rule:** every difference between a real PLC and the reference spec (`docs/mc_reference/mc-protocol-frame-spec.md`) or its golden vectors gets an entry here. While an entry is `open`, its step ids are listed in the profile's `tests/vectors/captured/<profile>/divergences.txt` and the replay test reports them as `known-divergence`. The owner decides each entry; golden vectors from Appendix A are never edited, only tagged.

## Questions to answer on hardware

Pre-filled from the reference spec Â§10.1 and ideas Â§5; answered from the 11 captures imported by T-089 (see "Where the evidence is").

| Question | Source | Answered by | Status |
|---|---|---|---|
| F3 short responses (`QACK`/`GG` without data, `QNAK`/`NN`) carry a SUM? | Â§10.1 Q1 | G8-Q1 (3C only on this bench) | **observed: no SUM** - F-004 |
| F2 response echoes the request's block number? | Â§10.1 Q2 | G8-Q2 (3C only on this bench) | **observed: yes** - F-005 |
| 1E ASCII odd bit write: N characters or N + dummy? | Â§10.1 Q3 | G8-Q3 | **observed: N + dummy `'0'`** - F-001 (codec changed) |
| Binary odd bit write: padding nibble ignored? | Â§10.1 Q4 | G8-Q4, G2-03 | **observed:** the Q03UDE ignores it; the FX5U and the FX3U-ENET-ADP close the connection - F-006 |
| 3E normal response route equals the request route? | Â§10.1 Q5 | G8-Q5 | **observed: yes** for the default route (312 of 312 normal 3E responses); a non-default station is only answered with an error (`C05F`) by the Q03UDE - F-007 |
| Error-code meanings | Â§10.1 Q6 | every plcError (table below) | **table filled**; meanings found only for the FX3 1E codes; the Q-series and FX5 codes have none in the manual dumps |
| 1E point limits (PDF Appendix 5 only) | Â§10.1 Q7 | G8-Q7, G1-03, G3-05 (on FX3) | **observed:** the FX3U-ENET-ADP rejects 256-word requests and a 256-bit write (`57H`), and a 41-word write to a bit device closes the connection - F-003. G8-Q7 and G8-Q7-R (the "limit accepted" side) were skipped ("scratch too small"), so the exact limits are the manual's, not measured |
| M9008 accepted for word access on 1E/1C? | Â§10.1 Q8 | G8-Q8 | **not testable on this bench** (no CPU exposing M9000â€“M9255 through 1E/1C); the spec rule 9000 + 16k stays |
| 3E Binary gives correct bytes on a real PLC | ideas Â§5 | GV-01â€¦05 on a 3E Binary profile | **observed: yes.** RPL-05 (b) (request and default-value response equal Appendix A) reports no difference on `q03ude-eth-3e-bin`, `q03ude-eth-3e-ascii`, `fx5u-eth-3e-bin` and `fx5u-eth-3e-ascii` |
| 1C and 3C work on the wire (never verified by the old app) | ideas Â§5 | GV, G1, G2 on serial profiles | **3C observed** on the Q03UDE + QJ71C24N (formats 1 with and without sum, 2, 3, 4). **1C not captured** (no FX3 serial profile imported); 1C formats 2 and 3 stay **wire-unverified** |
| `bitsAsWords` past the end of a bit device range fails every round | core-session open question 4 | G5-05 | **not run:** skipped in all 11 captures ("requires endNotMultiple16:M") |

## Where the evidence is

All paths are under `tests/vectors/captured/`. Each folder has `run.meta`, `steps.vec`, `session.vec`, `bench.csv` and `divergences.txt`; the `FILL:` notes in `run.meta` are the owner's and are unchanged. A "capture id" is the `# id:` line of `steps.vec`, `CAP-<profile>-<step>`; a step id with a suffix (`G2-01.3`, `G7-01+2`) belongs to its base step, and `divergences.txt` tags base steps.

| Profile | PLC / module | Frame |
|---|---|---|
| `q03ude-eth-3e-bin`, `q03ude-eth-3e-ascii` | Q03UDECPU, built-in Ethernet | 3E |
| `q03ude-c24-3c-f1-sum`, `-f1-nosum`, `-f2`, `-f3`, `-f4` | Q03UDECPU + QJ71C24N | 3C |
| `fx5u-eth-3e-bin`, `fx5u-eth-3e-ascii` | FX5U-32M, built-in Ethernet | 3E |
| `fx3-eth-1e-bin`, `fx3-eth-1e-ascii` | FX3U-32M + FX3U-ENET-ADP | 1E |

Manual quotes come from the text dumps of the vendor PDFs made for T-089 (`build/_scratch-leader/fx3-enet.txt` = FX3U-ENET-ADP manual, `fx5-eth.txt` = FX5 Ethernet manual; line numbers in those dumps). Where the dumps give no meaning, the entry says "meaning not found in the manuals". Status words: `decided` (the owner chose; codec or defaults changed), `decided - keep` (the PLC agrees with the spec, behaviour kept), `open` (no owner decision yet).

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
- **Profile / step:** `fx3-eth-1e-ascii` / GV-05, G2-01, G2-03, G8-Q3 (and the poll steps G6, G7-06, whose ad-hoc one-bit write is one character longer now).
- **Spec says:** (before 2026-10-14) Â§10.1 Q3: send exactly N characters for N points. Now: append one dummy `'0'` after an odd number of points.
- **PLC does:** the FX3U-ENET-ADP gives no answer to an odd bit write without the dummy. `CAP-fx3-eth-1e-ascii-GV-05`: `WriteBits M100 x5`, request `30 32 46 46 30 30 30 41 34 44 32 30 30 30 30 30 30 30 36 34 30 35 30 30 31 30 31 30 31` (5 data characters `31 30 31 30 31`), outcome `timeout`, `waited_ms: 3501.133`; the read-back `GV-05.2-R` is `38 30 30 30 30 30 30 31 30 30` = bits `0 0 0 1 0 0` (the old M100 pattern), so the write was not applied. The same for `G2-01` (3501.164 ms), `G2-01.3` (3501.826), `G2-03` (3502.308), `G2-03.2` (3501.381) and `G8-Q3` (3500.985). Even counts (GV-03/GV-04 M100 x8, G2-02 x16) are answered. Manual, FX3U-ENET-ADP "7.5.3 Batch write in bit units", ASCII caution: "If the number of device points to be set is an odd number, add one byte of dummy data (30H) at the end of data written" (`fx3-enet.txt:5051-5052`; binary: 4 bits of dummy data, lines 5004-5005). Poll steps: `session.vec` G6 tx frames end `... 30 31 30 30 31` (one data character).
- **Replay effect (what is tagged):** the codec now sends the dummy (T-087), so RPL-01 cannot re-encode these requests (the message reads "the mock cannot decode the captured request ..."), and RPL-02/RPL-03 of the same steps differ; RPL-04 of G6 and G7-06 differs by one character (captured 25 bytes, replay 26). Tagged until the profile is re-captured.
- **Status:** decided
- **Decision (owner, 2026-10-14):** change the codec: append the dummy `'0'` (T-087). Re-capture `fx3-eth-1e-ascii`; the tags go away then. The current `fx3-eth-1e-ascii` capture is committed as it is and is replaced after a re-capture with the T-087 codec (owner, 2026-10-14).

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
- **PLC does:** end code `57H` for 256 words and for a 256-bit write: `CAP-fx3-eth-1e-bin-G1-03` (`WriteWords D100 x256`), `G1-03.2` and `G1-04` (`ReadWords D100 x256`): `plcError 0057`; `G2-04` (`WriteBits M100 x256`, request `02 FF 0A 00 64 00 00 00 20 4D 00 00 10 10 ...`): `plcError 0057`, while the 256-bit read `G2-04.2` is `ok`. The same in `fx3-eth-1e-ascii` (`G1-03`, `G1-04`, `G2-04`: `0057`). `G8-Q7-W` (`WriteWords M112 x41`, a bit device, mutated): `peerClosed` after 11.232 ms, no answer. Manual: command table "256 points / 32 words (512 points) / 160 points / 10 words (160 points)" (`fx3-enet.txt:4631-4649`, `4694-4712`). Counts between the manual limit and 255 were not probed; `G8-Q7` and `G8-Q7-R` were skipped ("scratch too small").
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
  - FX5U (`fx5u-eth-3e-bin-G8-Q4`) and FX3U-ENET-ADP (`fx3-eth-1e-bin-G8-Q4`, `02 FF 0A 00 64 00 00 00 20 4D 05 00 10 10 11`): no answer, `peerClosed` after 2.328 ms and 11.010 ms; the follow-up read fails with `LinkDown` (F-012).
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
- **Profile / step:** every Q03UDE and FX5U profile / G5-02, G5-03, G5-04; `fx3-eth-1e-*` / G5-04; the four `q03ude-c24-3c-*` profiles with a sum / G7-03; `q03ude-c24-3c-f3` / G8-Q1 (G8-Q1.2).
- **Spec says:** RPL-03: the mock answers exactly like the PLC.
- **PLC does / mock does:** out-of-range device numbers: the Q03UDE over 3E and 3C answers `4031` (`CAP-q03ude-eth-3e-bin-G5-02`, D12288; `G5-03`, D12287 x2; `G5-04`, M8192), the mock `C051`; the FX5U answers `C056` (`CAP-fx5u-eth-3e-bin-G5-02/03/04`), the mock `C051`; the FX3 answers end code `58H` for M7680 (`CAP-fx3-eth-1e-bin-G5-04`), the mock `5BH`. A 3C request with altered sum characters (`G7-03`) is answered `NAK 7F24` by the C24; the mock sends another code (first difference at byte 12 of the NAK: captured `46`, mock `31`).
- **Status:** open
- **Decision (owner, date):** open. A mock fix would add per-target error codes.

### F-009 - Mock fidelity: other-station route, iQ-R subcommand, random read and chained frames on the Q03UDE
- **Profile / step:** `q03ude-eth-3e-bin`, `q03ude-eth-3e-ascii` / G8-Q5, G9-01, G9-05, G9-05-M, G9-06; `q03ude-c24-3c-f1-sum` / G9-01; `-f2`, `-f3`, `-f4` / G9-01; all five `q03ude-c24-3c-*` / G9-05, G9-05-M.
- **Spec says:** RPL-03: the mock answers like the PLC.
- **PLC does / mock does:**
  - Station 01 (G8-Q5): PLC `C05F`, mock `ok`.
  - iQ-R subcommand `0002` / `0003` on a Q (G9-05, G9-05-M): over 3E the PLC answers `plcError C059` with info `.../0401/0002` and `.../0401/0003`, the mock `ok`; over 3C the C24 answers `NAK 7F22`, the mock a data frame.
  - Random read 0403 (G9-01): the Q03UDE over 3E answers 21 bytes (`q03ude-eth-3e-bin-G9-01-B-R`, length field `0C 00`), the mock 20 (length `0B`). Over 3C F2 / F3 / F4 the C24 answers with data (36 / 38 / 36 bytes), the mock with a NAK; over 3C F1 with sum the C24 does not answer (`timeout`) and the mock sends a NAK.
  - Two requests in one segment (G9-06): the PLC answers once (13 bytes), the mock twice (26).
- **Status:** open
- **Decision (owner, date):** open.

### F-010 - Mock fidelity: `MockPlc` ignores the profile's `supports` list and special device ranges
- **Profile / step:** `q03ude-*` (3E and 3C) / G4-02-STS, -STC, -STN, -R, -ZR; `fx5u-eth-3e-*` / G4-02-V, -DX, -DY, -ZR; `fx3-eth-1e-*` / G4-02-F, -B, -W, -TC, -CC and G0-02, G1-09, G2-10, G3-04, G5-02.
- **Spec says:** SPEC-hil-capture: the sweep "also probes the family's other types and expects a PLC error"; `specialFrom` names a special range beyond `deviceEnd` that exists.
- **PLC does / mock does:**
  - Types outside `supports`: the Q03UDE answers `4031` (3E `plcError 4031`, 3C NAK 4031; `CAP-q03ude-eth-3e-bin-G4-02-R`, `-ZR`, `-STS`, `-STC`, `-STN`), the FX5U `C05C` (V, ZR; DX, DY in the Binary profile) or `C050` (DX, DY in the ASCII profile), the FX3 end code `56H` (`CAP-fx3-eth-1e-bin-G4-02-F`, `-B`, `-W`, `-TC`, `-CC`). The mock answers `ok` with data.
  - FX3 special range (`D=7999` in `device_end`, special from D8000 / M8000): the PLC answers `ok` for `D8010` (G0-02), `D8000` x4 (G1-09), `M8000` x16 (G2-10), `M8000` as word (G3-04) and `D8000` (G5-02, the plan expects `ok`); the mock answers `5BH` because those numbers lie beyond `deviceEnd`.
- **Status:** open
- **Decision (owner, date):** open. The mock would have to honour `supports` and `specialFrom` of the profile.

### F-011 - Mock fidelity: the FX3U-ENET-ADP answers an unknown subheader with `FF 50`
- **Profile / step:** `fx3-eth-1e-bin` / G7-02-B, `fx3-eth-1e-ascii` / G7-02-A.
- **Spec says:** (no rule; the mock stays silent for a frame it does not decode.)
- **PLC does:** a request with the subheader changed to `7F` gets a two-byte answer `FF 50` (Binary, `CAP-fx3-eth-1e-bin-G7-02-B-R`) or `46 46 35 30` = "FF50" (ASCII, `CAP-fx3-eth-1e-ascii-G7-02-A-R`); the library reports `protocolError FrameMismatch`. The mock sends nothing.
- **Status:** open
- **Decision (owner, date):** open.

### F-012 - Tool: the next step is sent before the link is up again after a peer close
- **Profile / step:** `fx3-eth-1e-ascii` / G4-03, G5-01; `fx3-eth-1e-bin` / G8-Q4.2; `fx5u-eth-3e-bin` / G8-Q4.2, G8-Q5; `fx5u-eth-3e-ascii` / G8-Q5.
- **Spec says:** "On `LinkFault` the tool reconnects (`connectToPlc()`), records the recovery, and continues with the next step" (SPEC-hil-capture, capture tool).
- **PLC does / tool does:** after a step that ended `peerClosed` (the PLC closed the connection), the next step is sent at once and recorded `transportError LinkDown`, `waited_ms` 2.3 to 10.9 (e.g. `CAP-fx3-eth-1e-bin-G8-Q4.2`, `CAP-fx5u-eth-3e-bin-G8-Q5`); the reconnect (`run.meta` `recovery.N`) follows after that step. Such a step says nothing about the PLC (in `G8-Q4.2` the read-back of the Q4 test is lost). `fx3-eth-1e-ascii` G4-03 (`LinkDown` after 6.3 ms, following five `0056` answers) and G5-01 (`LinkDown` after 2.587 ms, `verdict: failed`) are the same symptom; no `peerClosed` step is recorded just before G4-03, so the cause of the closed link there is not shown by the capture. The mock answers these steps, so RPL-03 differs.
- **Status:** open
- **Decision (owner, date):** open. The fix is in `hil_capture`: wait for the link before the next step, or repeat the step after the reconnect.

### F-013 - PLC behaviour: FX3U-ENET-ADP and FX5U close the TCP connection after some requests; the mock answers
- **Profile / step:** `fx5u-eth-3e-bin` / G8-Q4, G9-01-B, G9-05, G9-05-M, G9-06; `fx5u-eth-3e-ascii` / G9-01-A, G9-05, G9-05-M, G9-06; `fx3-eth-1e-bin` / G8-Q4, G8-Q7-W; `fx3-eth-1e-ascii` (run.meta recoveries). Capture ids: `CAP-fx5u-eth-3e-bin-G9-05`, `CAP-fx5u-eth-3e-ascii-G9-05`, `CAP-fx3-eth-1e-bin-G8-Q4`.
- **Spec says:** (nothing; the mock never closes a connection on a request it understands.)
- **PLC does:** after these requests the PLC sends no response and closes the connection: outcome `peerClosed` (`waited_ms` 2.0 to 11.2) and a `PeerClosed` recovery in `run.meta`. `PeerClosed` recoveries: 9 of 11 in `fx5u-eth-3e-bin`, 6 of 8 in `fx5u-eth-3e-ascii`, 4 of 6 in `fx3-eth-1e-bin`, 8 of 14 in `fx3-eth-1e-ascii`, none in the Q03UDE profiles (they answer the same probes with an error, F-009). The FX3U-ENET-ADP manual lists closes on the existence-confirmation timeout, on RST / close, and when the external device does not acknowledge (`fx3-enet.txt:3474-3528`); a close after a rejected request is not listed in the dumps, so the cause is not stated.
- **Replay effect:** RPL-03 differs where the mock answers (G8-Q4, G9-* on the FX5U).
- **Status:** open
- **Decision (owner, date):** open.

### F-014 - withdrawn 2026-10-14 (owner): not recorded. The id is not reused.

### F-015 - Tool: the RPL-05 read-back check cannot follow the plan's `then` chains
- **Profile / step:** every profile / GV-01, GV-03, G2-01; `q03ude-c24-3c-f3` / G8-Q1.
- **Spec says:** RPL-05 (a): "every `write` step is followed by a read-back that matched".
- **Checker does:** `tests/hil/replay/replay.cpp` `checkSanity` (a) accepts only reads of the same base step (`rd.step != w.step` skips) and keeps the last value read. `GV-01` (write D100 x3) and `GV-03` (write M100 x8) are read back by the next steps `GV-02` / `GV-04.2`, so the same-step search finds nothing ("the write of D100 x3 has no read-back ... (the plan needs readBack)"). `G2-01` writes 1, reads, writes 0, reads: the checker compares the first write (1) with the last read (0): "the read-back of M100 is 0 but 1 was written". `G8-Q1` writes without a read in the step. The PLCs read back correctly (`CAP-q03ude-eth-3e-bin-G2-01.2-R` bit 1, `G2-01.4-R` bit 0).
- **Status:** open
- **Decision (owner, date):** open.

### F-016 - Tool: RPL-05 compares the request with the Appendix A vector even when the profile differs
- **Profile / step:** `q03ude-c24-3c-f1-nosum` / GV-02, GV-03; `q03ude-c24-3c-f2` / GV-02, GV-03. Capture ids: `CAP-q03ude-c24-3c-f1-nosum-GV-02`, `CAP-q03ude-c24-3c-f2-GV-02`.
- **Spec says:** RPL-05 (b): "every GV step's bytes equal the Appendix A vector it mirrors (request always; response when the route and values are the defaults)".
- **Checker does:** the vectors `V-3C1-01/03` carry a sum check and `V-3C2-01/03` the block number `00`. The profile `f1-nosum` has no sum (the capture request is 31 bytes against the vector's 33 for GV-02, 39 against 41 for GV-03) and `f2` uses block `5A` (byte 1 `35` against `30`). The request differs from the vector for a documented reason, and the check reports it. `f1-sum` and the 3E profiles equal the vectors.
- **Status:** open
- **Decision (owner, date):** open. The check would skip or adapt for `sumCheck=false` and `blockNo != 0`.

### F-017 - Tool: the EOT step outcome is recorded `ok` while the PLC answered a NAK
- **Profile / step:** the five `q03ude-c24-3c-*` profiles / G7-01+2.
- **Spec says:** (G7-01: a truncated request, then EOT, then a normal request.)
- **Tool does / PLC does:** the EOT (`04`) after the truncated request (`CAP-q03ude-c24-3c-f1-sum-G7-01+2`) is answered `15 46 39 30 30 30 30 46 46 30 30 37 46 32 34` = NAK, `F9`, `0000`, `FF`, `00`, `7F24` (`f1-nosum` answers `7F22`, `f4` `7F23`, `f3` the same as a `QNAK 7F24` frame). The step outcome is `ok` with no `expect`. The parser reads the same bytes as `PlcError`, so RPL-02 reports "recorded outcome ok, the parser says failed with PlcError". The mock answers nothing to a lone EOT (RPL-03: captured 15 to 20 bytes, mock 0).
- **Status:** open
- **Decision (owner, date):** open.

### F-018 - Tool: RPL-02 parses a mutated step with the profile's FrameConfig, not the step's override
- **Profile / step:** `q03ude-c24-3c-f2` / G8-Q2-M. Capture id: `CAP-q03ude-c24-3c-f2-G8-Q2-M`.
- **Spec says:** RPL-01 skips steps that "ran with another FrameConfig"; RPL-02 should parse the response with the same config.
- **Checker does:** `G8-Q2-M` ran with `override: blockNo=91` (0x5B) and the PLC echoed `5B` (F-005). RPL-02 parses it with the profile's block `5A` and reports "recorded outcome ok, the parser says failed with FrameMismatch".
- **Status:** open
- **Decision (owner, date):** open.

### F-019 - Tool: RPL-04 session replay differs by 3 ms on one poll round
- **Profile / step:** `q03ude-c24-3c-f3` / G7-06. Capture id: the poll step `G7-06` in `session.vec` of `q03ude-c24-3c-f3`.
- **Spec says:** RPL-04: the replayed events match the recorded ones.
- **Checker does:** "event #9456: cycleDone round 3152: duration 41 ms captured, 38 ms replayed". The captured chunk times are in nanoseconds; the capture records 41 ms, the fake-clock replay 38 ms. A 1 ms rounding was the first guess, but the difference is 3 ms, so the cause is not established. All other rounds of this and the other poll steps match.
- **Status:** open
- **Decision (owner, date):** open.

### F-020 - Tool: RPL-03 does not model the PLC dropping an incomplete frame; after F-001 the mock swallows the following requests
- **Profile / step:** `fx3-eth-1e-ascii` / G1-01, G2-02, G2-06, G2-08, G3-01, G3-02, G4-01-D, -R, -M, -X, -Y, -TS, -TN, -CS, -CN (steps that fail only since T-087).
- **Spec says:** RPL-03: the steps are replayed in order into a `MockPlc`.
- **Checker does / PLC does:** since T-087 the mock waits for the dummy of an odd bit write (MCK-14). In the capture the PLC timed out after 3.5 s on `GV-05`, dropped the partial frame and answered the next request (`GV-05.2-R` is a normal read answer). The replay has no clock: the mock keeps waiting and takes the characters of the following requests as the missing data. From `GV-05.2` on, the mock's answers belong to earlier requests (e.g. `G1-01`: captured `83 00`..., mock a read answer `80 ...`). The steps above pass in `build/_scratch-leader/replay-all.log` (before T-087) and fail only now. Re-capturing the profile (F-001) removes the cause; dropping the partial frame at a captured timeout in the harness removes it for old captures.
- **Status:** open
- **Decision (owner, date):** open. The current `fx3-eth-1e-ascii` capture is committed as it is and is replaced after a re-capture with the T-087 codec (owner, 2026-10-14).

### F-021 - Bench note: Y0-Y7 of the Q03UDE are the QJ71C24N's own I/O
- **Profile / step:** all `q03ude-*` profiles (scratch in `run.meta`).
- **Spec says:** the first Q03UDE profile draft listed `Y0-Y7` as scratch.
- **PLC does:** the module's own I/O occupies Y0-Y7 on this bench (owner, `agent-team/memory/decisions.md`, 2026-10-12), so the captures of 2026-10-13/14 declare `scratch: ... Y10-Y17`. Earlier captures were not imported. No step is tagged.
- **Status:** open (information only; the owner may close it)
- **Decision (owner, date):** open.

## PLC error codes seen

Built from every `plcError` outcome of the 11 imported runs (Â§10.1 Q6). The reference spec returns the raw code and holds no meaning table, so no code below is already known to it. Meanings are quoted from the manual dumps with a line reference; "meaning not found in the manuals" means the dumps give none (the Q03UDE / QJ71C24N manuals are not among them). The library does not interpret codes.

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
| `7F24` | 3C (QJ71C24N, F1 sum, F2, F3, F4) | G7-03 (altered sum characters); in `f1-sum`, `f2`, `f3` also the answer to the EOT in G7-01+2 | meaning not found in the manuals |
| `C050` | 3E ASCII (FX5U) | G4-02-DX, G4-02-DY (read DX0 / DY0) | not resolvable from the dump: the table "SLMP function error code" (`fx5-eth.txt:13295-13334`) lists C050H to C061H in one column and the descriptions in another, and the row alignment is lost in the text. Meaning not found in the dump; read the PDF page 272 |
| `C052` | 3E ASCII (FX5U) | G1-03.2, G1-04: read D100 x960 | same dump problem (`fx5-eth.txt:13303-13334`); the context is a point count above the ASCII limit (F-002) |
| `C056` | 3E (FX5U, Binary and ASCII) | G5-02 (D8000), G5-03 (D7999 x2), G5-04 (M7680), numbers beyond `deviceEnd` | same dump problem (`fx5-eth.txt:13303-13334`) |
| `C05C` | 3E (FX5U, Binary and ASCII) | G4-02-V, -ZR (both profiles); G4-02-DX, -DY (Binary profile only): types outside `supports` | same dump problem (`fx5-eth.txt:13303-13334`) |
