# HIL Findings

- **Belongs to:** `docs/spec/SPEC-hil-capture.md` (decision H3)
- **Rule:** every difference between a real PLC and the reference spec (`docs/mc_reference/mc-protocol-frame-spec.md`) or its golden vectors gets an entry here. While an entry is `open`, its step ids are listed in the profile's `tests/vectors/captured/<profile>/divergences.txt` and the replay test reports them as `known-divergence`. The owner decides each entry; golden vectors from Appendix A are never edited, only tagged.

## Questions to answer on hardware

Pre-filled from the reference spec §10.1 and ideas §5. Move each to "Findings" once observed, even when the PLC agrees with the spec.

| Question | Source | Answered by | Status |
|---|---|---|---|
| F3 short responses (`QACK`/`GG` without data, `QNAK`/`NN`) carry a SUM? | §10.1 Q1 | G8-Q1 (3C only on this bench) | to verify |
| F2 response echoes the request's block number? | §10.1 Q2 | G8-Q2 (3C only on this bench) | to verify |
| 1E ASCII odd bit write: N characters or N + dummy? | §10.1 Q3 | G8-Q3 | to verify |
| Binary odd bit write: padding nibble ignored? | §10.1 Q4 | G8-Q4, G2-03 | to verify |
| 3E normal response route equals the request route? | §10.1 Q5 | G8-Q5 | to verify |
| Error-code meanings | §10.1 Q6 | every plcError (table below) | collecting |
| 1E point limits (PDF Appendix 5 only) | §10.1 Q7 | G8-Q7, G1-03, G3-05 (on FX3) | to verify |
| M9008 accepted for word access on 1E/1C? | §10.1 Q8 | G8-Q8 | **not testable on this bench** (no CPU exposing M9000–M9255 through 1E/1C); the spec rule 9000 + 16k stays |
| 3E Binary gives correct bytes on a real PLC | ideas §5 | GV-01…05 on a 3E Binary profile | to verify |
| 1C and 3C work on the wire (never verified by the old app) | ideas §5 | GV, G1, G2 on serial profiles | to verify; 1C formats 2 and 3 stay **wire-unverified** (not on this bench) |
| `bitsAsWords` past the end of a bit device range fails every round | core-session open question 4 | G5-05 | to verify |

## Findings

One entry per observed difference or confirmed behaviour. Ids are never reused.

<!--
### F-001 — <one-line title>
- **Profile / step:** q03ude-c24-3c-f3 / G8-Q1
- **Spec says:** …
- **PLC does:** … (capture: tests/vectors/captured/q03ude-c24-3c-f3/steps.vec, id CAP-…)
- **Status:** open | decided
- **Decision (owner, date):** change codec | change FrameConfig default | keep, document | …
-->

_None yet._

## PLC error codes seen

Built from every `plcError` outcome of every run (§10.1 Q6). Meanings are filled in by the owner from the module manuals; the library does not interpret them.

| Code | Frame / module | Step and request that caused it | Meaning (from manual) |
|---|---|---|---|
| | | | |
