# Replay fixtures: captures of `virtual_plc`, not hardware

These folders are capture sets written by `tools/hil_capture` against `examples/virtual_plc` (the
`MockPlc` behind a program). They are **not hardware captures**: their `run.meta` operator note says
"virtual_plc fixture, not hardware", and hardware captures go to `tests/vectors/captured/`, which
`mc_replay_tests` sweeps by default. The fixtures exist so that the replay checks RPL-01 to RPL-06 and
their negative tests run on every build, with no PLC.

| Folder | Frame | Link | Plan | Profile |
|---|---|---|---|---|
| `vplc-3e-bin/` | 3E Binary | loopback TCP | `plans/fixture_3e.json` | `profiles/vplc-3e-bin.json` |
| `vplc-3e-bin-fault/` | 3E Binary | loopback TCP through a proxy that drops the answers of the poll's connection (a timeout fault, then a reconnect and a restart) | `plans/fixture_3e_fault.json` | `profiles/vplc-3e-bin-fault.json` |
| `vplc-3e-bin-drop/` | 3E Binary | loopback TCP through a proxy that closes the poll's connection while an ad-hoc write is outstanding (peer closed, then a reconnect and a restart) | `plans/fixture_3e_drop.json` | `profiles/vplc-3e-bin-drop.json` |
| `e2e-3e-bin-20ms/` | 3E Binary | loopback TCP, cycle interval 20 ms, timeout 400 ms | `tests/hil/e2e/plan_3e.json` (with the E-10 reconnect) | the e2e test profile |
| `vplc-3c-f4/` | 3C format 4 | virtual COM pair `COM54` / `COM55`, 9600 baud 7E1 | `plans/fixture_3c.json` | `profiles/vplc-3c-f4.json` |
| `vplc-fx-3e-ascii-oct/` | 3E ASCII, X/Y octal (`frame.xyNotation` and `frame.xyAsciiDigits` `Octal`) | loopback TCP | `plans/fixture_fx_3e_oct.json` | `profiles/vplc-fx-3e-ascii-oct.json` |

The plans derive from the end-to-end plans of `tests/hil/e2e/`: `fixture_3e` adds the Appendix A
mirror steps `GV-01` to `GV-05`; neither has a reconnect step (virtual_plc gives every TCP connection a
fresh `MockPlc`) nor a bench step. A timing in these files is a loopback or virtual-port timing. The
`session.vec` of all of them holds the recorded poll inputs (`kind: input` records), which RPL-04 feeds
to the Session; every response record carries its `verdict`.

`vplc-3e-bin-fault/` and `vplc-3e-bin-drop/` are the shape of catalogue step G7-06 (the cable pulled in
the middle of a poll): the poll's link faults, the tool reconnects and the poll restarts, so `session.vec`
holds a `linkFault`, `linkState` changes and a second run of rounds numbered from 1 again. Their profiles
differ from `vplc-3e-bin` in a 400 ms response timeout and a 50 ms cycle interval, and in the port: the
tool connects to a proxy on 5063 that forwards to `virtual_plc` on 5064.

`e2e-3e-bin-20ms/` is the capture the HIL-04 test wrote in a run where the replay failed (the tick that started round
2 was made 0.4 ms before the 20 ms deadline of the replay clock): it keeps that timing as a regression test for RPL-04.

## Regenerating

Re-run only when the capture format or the tool's recording changes on purpose. Use a folder under
`build/` as the output root, check the result with `mc_replay_tests --replay-root=<that folder>`, then
copy the folders here (`tests/vectors/captured/` must stay empty of virtual_plc data).

```powershell
# 3E over loopback TCP (the profile's port is 5061)
examples/virtual_plc --frame 3E --code Binary --port 5061 --set D100=1234
"vplc-3e-bin" | tools/hil_capture --profile tests/hil/fixtures/profiles/vplc-3e-bin.json `
    --plan tests/hil/fixtures/plans/fixture_3e.json --output-root build/_scratch-fixtures `
    --note "virtual_plc fixture, not hardware"

# FX numbering: 3E ASCII with octal X/Y text and octal digits (port 5065); step F-01 reads X10, so
# virtual_plc is started with X10 and X12 set
examples/virtual_plc --frame 3E --code ASCII --xy octal --xy-ascii octal --port 5065 --set D100=1234 --set X10=1 --set X12=1
"vplc-fx-3e-ascii-oct" | tools/hil_capture --profile tests/hil/fixtures/profiles/vplc-fx-3e-ascii-oct.json `
    --plan tests/hil/fixtures/plans/fixture_fx_3e_oct.json --output-root build/_scratch-fixtures `
    --note "virtual_plc fixture, not hardware"

# 3C format 4 over the virtual COM pair (virtual_plc answers on COM55, the tool opens COM54)
examples/virtual_plc --frame 3C --format 4 --serial COM55 --set D100=1234
"vplc-3c-f4" | tools/hil_capture --profile tests/hil/fixtures/profiles/vplc-3c-f4.json `
    --plan tests/hil/fixtures/plans/fixture_3c.json --output-root build/_scratch-fixtures `
    --note "virtual_plc fixture, not hardware (virtual COM pair)"

# 3E with a link fault in the poll: virtual_plc on 5064, the proxy on 5063 faults the 2nd connection
# (the 1st is the read L-01; the poll is the 2nd). make/fault_proxy.cpp is the CMake target hil_fault_proxy (tests + tools on), see its header.
examples/virtual_plc --frame 3E --code Binary --port 5064 --set D100=1234
hil_fault_proxy 5063 5064 stall 275 2
"vplc-3e-bin-fault" | tools/hil_capture --profile tests/hil/fixtures/profiles/vplc-3e-bin-fault.json `
    --plan tests/hil/fixtures/plans/fixture_3e_fault.json --output-root build/_scratch-fixtures `
    --note "virtual_plc fixture, not hardware"

# 3E with the peer closing the link while an ad-hoc write is outstanding (same ports; the 2nd connection)
hil_fault_proxy 5063 5064 closeonwrite 0 2
"vplc-3e-bin-drop" | tools/hil_capture --profile tests/hil/fixtures/profiles/vplc-3e-bin-drop.json `
    --plan tests/hil/fixtures/plans/fixture_3e_drop.json --output-root build/_scratch-fixtures `
    --note "virtual_plc fixture, not hardware"
```

`hil_fault_proxy` (source `make/fault_proxy.cpp`) is built with `MC_BUILD_TESTS` and `MC_BUILD_TOOLS` so it cannot rot. It is a fixture-generation helper, not a test: no ctest entry and no qmake twin.

`virtual_plc` is started with `--set D100=1234` because step `E-01` reads that value before anything
writes it (the replay seeds memory the run did not write from the captured reads). The capture of a
fault depends on timing (which round the proxy stops answering in), so a re-run gives a different
capture of the same shape; the tests rely on the shape, not on a round number.
