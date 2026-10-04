# Spec: gui-tool

- **Module id:** `gui-tool`
- **Status:** approved by the owner, 2026-10-03 (open questions answered the same day)
- **Depends on:** `qt-device` (`McDevice`, `Transport`, `McDeviceConfig`), `mock-plc` (`MockPlc`), `hil-capture` (the `mc_hil_tool` library: profile, plan, safety gate, runner, capture writer, report), `core-*`
- **Depended on by:** nothing (a developer and bench tool)
- **Inputs:** owner request 2026-10-03 ("GUI tool hỗ trợ chạy test, debug trace và capture frame, hil, hỗ trợ mock plc, dùng qt có config qmake và cmake, dùng property browser trong folder components …, docking system (C:\build_packages, setup qua local path …) hỗ trợ mở nhiều tab, chạy song song nhiều McDevice, mock plc … GUI chạy McDevice và các object khác trên một runner thread khác main thread để tránh làm crash app"); owner answers 2026-10-03 (qpb vendored in `components/qpb`, the GUI may require Qt 6.5, phase 8 before bench and release); `SPEC-qt-device.md` (threading rules), `SPEC-mock-plc.md`, `SPEC-hil-capture.md`.

## Objective

A Qt Widgets desktop application, working name **MC Workbench** (`tools/mc_workbench`), that makes the library visible: connect to PLCs and to built-in mock PLCs, watch and change device values, see every frame on the wire, record captures and run the HIL plans — several of them side by side, each in its own tab. It is the **first application that uses `McDevice`**: friction found here is fed back to `qt-device` as findings, not patched around.

**User stories**

- As the owner at the bench, I open a tab per PLC, edit its `McDeviceConfig` in a property grid, connect, subscribe a few ranges and watch the values change, without writing code.
- As a developer, I start two mock PLCs (one TCP, one on a COM port) and two `McDevice` tabs against them, inject a fault in one mock and watch the other tab keep running.
- As a developer, I see the debug trace of a session: every tx/rx chunk with its time stamp and hex dump, the `Session` log lines (`LogSink`), link state changes and faults, filtered per tab and level, and the mock's view (request log, `eotCount()`, `skippedBytes()`).
- As the owner, I run a HIL plan (`tests/hil/plans/*.json`) against a profile from the GUI with the **same safety gate** as `hil_capture`, see each step's outcome live, and replay the capture.
- As anyone, a device that misbehaves (timeouts, a broken COM port, a throwing slot) never freezes or crashes the window; closing a tab stops its thread cleanly.

## Functions

| Area | What the GUI does | Reuses |
|---|---|---|
| Device tab | `McDeviceConfig` in a qpb property grid (TCP / serial, frame, code, format, session); connect / disconnect; link state and faults; subscriptions table with live values (changed values highlighted) and a small custom trend widget for selected values (no Qt Charts; owner 2026-10-03); ad-hoc read / write console with results and correlation ids | `McDevice`, `McDeviceConfig` JSON |
| Mock PLC tab | A `MockPlc` served over TCP (listen port) or a COM port; config in qpb; memory editor (device ranges as tables); fault injection and corruption (`mute`, `failRange`, `Corruption` modes); request log; `eotCount()`, `skippedBytes()` | `MockPlc`; its own small serving code in the GUI (`examples/virtual_plc` stays unchanged, owner 2026-10-03) |
| Frame trace | Per tab: tx / rx chunks with nanosecond stamps, hex and ASCII view, frame boundaries, optional decode (op, device, count, outcome) | `RecordingTransport` from `mc_hil_tool` |
| Debug log | `LogSink` lines of every `Session`, `McDevice` and mock, with level and tab filters, search, copy, save | `LogSink` |
| Capture and export | Record a tab's traffic to `steps.vec` / `session.vec` / `run.meta` in the capture formats of `hil-capture`, so a capture from a real PLC can be **exported as a replay test** (`tests/vectors/captured/<profile-id>/`, read by `mc_replay_tests`; the owner confirms the files before `git add`). Captures of mocks or `virtual_plc` go to a user folder, never to `tests/vectors/captured/` | `CaptureWriter` |
| HIL runner | Load a profile and a plan, run the safety gate, show the dry run, confirm by typing the profile id. Only a **loopback** profile (TCP host `127.0.0.0/8`, `::1` or `localhost`; a mock or `virtual_plc`) whose run has no `readOnly` frames may be confirmed without typing, like `hil_capture --yes`; any other profile (another host, any COM port) always needs the typed id (amended 2026-10-04, owner decision C), run, show step outcomes live, open the capture, run the replay, show the bench report | `mc_hil_tool` (`profile`, `plan`, `safety_gate`, `runner`, `bench_report`), `mc_replay_tests` |
| Workspace | Save / load the set of tabs and their configs as one JSON file; the dock layout is saved and restored | `McDeviceConfig` JSON, ADS state |

**"Chạy test" (running tests) means** running HIL plans and replays from the GUI, and exporting real-PLC captures as replay test data. The GUI does **not** launch the unit-test binaries (owner decision 2026-10-03).

## Architecture

```text
GUI thread (main)                               runner threads (one QThread per tab)
┌───────────────────────────────┐   queued     ┌──────────────────────────────────────┐
│ MainWindow + ADS DockManager   │  signals /   │ DeviceRunner (QObject)               │
│  ├─ DeviceTab / MockTab views  │◀────────────▶│  owns McDevice + RecordingTransport  │
│  ├─ qpb property grids         │  invoke-     │  + its timer; batches values/frames  │
│  ├─ trace / log views (models) │  Method      │ MockRunner (QObject)                 │
│  └─ HIL runner view            │              │  owns MockPlc + QTcpServer/QSerialPort│
└───────────────────────────────┘              │ HilRunner (QObject) owns the runner   │
                                                └──────────────────────────────────────┘
```

- **Threading rule (owner requirement):** every `McDevice`, transport, `MockPlc` server and HIL run lives on a **runner thread**, never on the GUI thread. One `QThread` per tab. The runner object is created **inside** its thread (`QThread::started`), so `McDevice` and its children are born there (`SPEC-qt-device.md`: one object tree on one thread, no mutex).
- The GUI never calls a runner object directly: commands go through queued `QMetaObject::invokeMethod`, results come back through queued signals carrying **value copies** (no pointers into runner-owned memory).
- **Back-pressure:** runners batch high-rate output (values, frames, log lines) and emit at most ~30 times per second per tab; views keep bounded ring buffers (configurable, e.g. 100 000 trace lines) so a chatty link cannot grow memory without limit.
- **Containment:** a runner catches every exception at its slot boundary, reports it as a tab error and stops that tab's device; the GUI stays responsive. A stuck call in a runner (e.g. a driver call) cannot freeze the window; closing a tab requests a stop, waits up to a bounded time, and reports a stuck thread instead of blocking the GUI.
- **Shutdown:** closing a tab or the app disconnects, stops and joins every runner thread in order; no object is deleted from a foreign thread (`deleteLater` on its own thread).
- The GUI code uses only public library API (as `hil_capture`, decision D1 of Phase 7). A needed library change is reported to the owner.

## Build and third-party components

- **Location:** `tools/mc_workbench/` (app) with a non-UI part (`runners`, models) compiled into a static library so tests can use it without widgets.
- **CMake:** option `MC_BUILD_GUI` (default `ON` when top-level, `MC_BUILD_TOOLS` is on and the docking library is found; otherwise the GUI is skipped with a status message). **qmake:** `tools/qmake/mc_workbench.pro`, part of `mc_protocol.pro` when the docking library path is set.
- **Qt:** the GUI requires **Qt ≥ 6.5** (qpb's minimum; owner decision 2026-10-03). The library and the other tools keep Qt 6.2.
- **qpb** (property browser, MIT, 1.6.1): vendored unmodified in `components/qpb` (owner decision 2026-10-03: renamed from `componentes`, committed). CMake: `add_subdirectory(components/qpb)`, link `qpb::widgets`. qpb ships no qmake files, so a wrapper `components/qpb.pri` **outside** the vendored folder lists its sources for qmake.
- **Qt Advanced Docking System** (ADS 5.1.1, LGPL-2.1, linked dynamically as a DLL): **not** copied into the repo. Each machine points at its own build through a **local path**:
  - CMake: cache variable `MC_ADS_DIR` (default: environment variable `MC_ADS_DIR`, else `C:/build_packages/qtadvanceddocking-5.1.1`), added to `CMAKE_PREFIX_PATH` for `find_package(qtadvanceddocking-qt6)`.
  - qmake: a git-ignored `mc_local.pri` at the repo root (template `mc_local.pri.example` committed) setting `MC_ADS_DIR`; the `.pro` reads it.
  - The ADS DLL (debug `qtadvanceddocking-qt6d.dll` / release `qtadvanceddocking-qt6.dll`) is copied next to the executable after the build (or put on `PATH` for tests).
  - The GUI builds on **MSVC and MinGW** (owner decision 2026-10-03). MSVC uses the prebuilt ADS in `C:/build_packages/qtadvanceddocking-5.1.1`. For MinGW, a script `scripts/build-ads.ps1` builds ADS from its source (`MC_ADS_SOURCE_DIR`, default `C:/build_packages/Qt-Advanced-Docking-System`, read only) with the chosen kit into a folder **inside the project** (`build/ads-<kit>/install`), and `MC_ADS_DIR` / `mc_local.pri` point there. Nothing is written outside the project.
- `.gitignore` gains `mc_local.pri` (owner confirms, Ask first).
- LGPL note: ADS is used as a separately supplied shared library; its license file is shipped next to the DLL in any distribution of the tool.

## Testing

QtTest binary `mc_workbench_tests`, label `gui`, run with `QT_QPA_PLATFORM=offscreen`:

| ID | Covers |
|---|---|
| GUI-01 | `DeviceRunner` on its own thread connects to an in-process `MockRunner` over loopback, subscribes, delivers values to the GUI thread as copies; `McDevice` lives on the runner thread (asserted with `QObject::thread()`) |
| GUI-02 | Several device tabs and mocks in parallel (e.g. 4 + 2), independent link states; a fault in one mock does not disturb the others |
| GUI-03 | Containment: a runner slot that throws, a transport that fails to open, a COM port that is busy — the tab reports the error, the GUI thread keeps processing events (measured), no crash |
| GUI-04 | Shutdown: closing tabs and the window joins every runner thread within the bound; no leak (object counts) and no cross-thread delete (debug assertion hooks) |
| GUI-05 | Back-pressure: a mock answering as fast as possible for N seconds keeps memory bounded and the GUI event latency below a limit |
| GUI-06 | qpb grid ↔ `McDeviceConfig` round trip: every field editable, invalid values rejected with the library's `validate()` message |
| GUI-07 | HIL runner: the same gate decisions as `hil_capture` on the committed plans (dry runs identical), `readOnly` confirmation requires the typed profile id, a non-loopback or COM profile always requires it, a refused plan sends nothing |
| GUI-08 | Workspace save / load restores tabs, configs and the dock layout |
| GUI-09 | Smoke: the main window opens offscreen with ADS and qpb, every dock widget is created, the app exits 0 |

Serial cases use `MC_TEST_SERIAL_PAIR` with `RESOURCE_LOCK mc_serial_pair`. A manual owner demo closes Checkpoint H.

## Boundaries

**Always**

- Every `McDevice`, transport, mock server and HIL run on a runner thread; the GUI thread only renders and forwards commands.
- The HIL safety gate is the `mc_hil_tool` gate, unchanged; the GUI adds no bypass. The typed confirmation can be skipped only as the HIL runner row says: a loopback profile and no `readOnly` frames (amended 2026-10-04, owner decision C).
- Bounded buffers for everything that streams.

**Ask first**

- Any change to the library's public API found necessary by the GUI.
- `.gitignore` (`mc_local.pri`), and any further third-party code.

**Never**

- Block the GUI thread on I/O or on a runner.
- Copy ADS into the repository (it stays a local-path dependency).
- Talk to a real PLC from automated tests.

## Open questions

1. **Q1 — tests from the GUI.** Besides HIL plans and replays, should the GUI also launch the unit-test binaries (`ctest` labels) and show their results? *Proposed:* no in v1 (the CLI and check scripts do it); maybe later.

  - Change decision, no unit-test on GUI. GUI export capture information use for unit test as real frame from PLC.

2. **Q2 — MinGW.** The ADS build on this PC is MSVC only. Build the GUI on MSVC only (proposed), or also build ADS from source (`C:/build_packages/Qt-Advanced-Docking-System`) for MinGW?

  - Build GUI on MSVC and also build ADS from source for mingw.

3. **Q3 — name.** "MC Workbench" / `mc_workbench` — keep or rename?

  - keep.

4. **Q4 — `virtual_plc`.** The mock serving code of `examples/virtual_plc` is needed by the GUI. *Proposed:* move it into a small shared tool library (`tools/mock_server`) used by both, keeping `virtual_plc`'s command line unchanged.

  - keep virtual PLC.

5. **Q5 — charts.** Value trends over time are useful; Qt Charts is GPL/commercial. *Proposed:* a small custom trend widget, or none in v1.

  - a small custom trend widget.

## Proposed task breakdown (baton ids assigned on approval)

1. Wiring: `MC_BUILD_GUI`, local ADS path (CMake + qmake), `scripts/build-ads.ps1` for MinGW, `components/qpb` + `qpb.pri`, empty main window with docking, GUI-09.
2. Runner threads: `DeviceRunner`, `MockRunner`, value copies, containment and shutdown; GUI-01, 03, 04.
3. Device tab: qpb config grid, connect, subscriptions table, ad-hoc console; GUI-06.
4. Mock tab: the GUI's own mock serving code, memory editor, faults, request log, counters; GUI-02.
5. Frame trace, debug log, back-pressure, capture to files; GUI-05.
6. HIL runner view on `mc_hil_tool`, replay and report; GUI-07.
7. Workspace save / load and layout; GUI-08.
8. Checkpoint H: both build systems, full verification, owner demo.
