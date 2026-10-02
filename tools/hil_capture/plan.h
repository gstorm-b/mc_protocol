/**
 * @file plan.h
 * @brief The plan: the machine form of one frame family's part of `COMMAND-CATALOGUE.md`.
 *
 * Plan JSON (schema 1). Every key not listed is an error.
 *
 *     { "schema": 1,
 *       "plan": { "id": "qna_ethernet", "title": "...", "family": "qna-ethernet" },
 *       "steps": [ STEP, ... ] }
 *
 * Common keys of a STEP:
 *   id        required; the catalogue id ("G1-03"); letters, digits, '-', '_', '.'. The group of
 *             a step is the part before the first '-' ("G1"); `--only G1,G2` selects groups.
 *   kind      required: write, read, poll, mutate, raw, bench.
 *   title     optional text.
 *   requires  optional array of conditions; a step whose condition fails is skipped, not an
 *             error: "supports:D", "scratch:Y", "scanTime", "frame:3E|1E|3C|1C",
 *             "code:Binary|Ascii", "format:1..4", "sumCheck:on|off", "profile:<id>" (* and ?
 *             match: "profile:*-noonline"), "endNotMultiple16:M" (deviceEnd + 1 is not a
 *             multiple of 16). A leading '!' negates a condition.
 *   foreach   optional: "supports" (one step per profile.supports type) or "other" (one step per
 *             device type the frame has a code for that `supports` lacks); "{T}" in a device
 *             text stands for the symbol. Expanded steps are named "<id>-<symbol>".
 *   frameOverride  optional object of FrameConfig keys (names as in the device JSON) applied for
 *             this step on top of the profile's frame; a number may be "+N" or "-N", relative to
 *             the profile's value (station + 1). The safety gate limits it (see below).
 *   scratchTooSmall  "refuse" (default) or "skip": a step whose scratch-relative devices do not
 *             fit one scratch range is skipped ("skipped: scratch too small") instead of
 *             refused. Meant for the limit steps (counts such as "Wmax").
 *   mirrors   optional text copied to the capture ("V-3E-{x}-05/06"; {x} = B or A by data
 *             code, {n} = serial format number).
 *   expect    "ok" | "plcError" | "timeout" | "noResponse" | "notSent" | "record", or an object
 *             { "kind": ..., "values": [...] | "1010", "bitsOn": [2,4], "valuesFrom": "G1-03",
 *             "frames": 2 }. Default: ok for write and read, record for the others.
 *   then      optional array of further operations after this one: { "kind": "read"|"write",
 *             OP fields, "expect": ... } (write, read, mutate and raw steps only).
 *
 * What a frameOverride may do (checked by the safety gate, see safety_gate.h):
 *   - on a raw or mutate step it may not change `frame`, `code`, `format`, `sumCheck` or the
 *     routing keys `network`, `pc`, `io`, `station`, `stationNo`, `selfStation`: the gate reads
 *     the bytes of such a step under the profile's frame, the PLC reads them as they are;
 *   - on any other step that writes (a write, a read with a `then` write, a poll with a
 *     heartbeat or a write action, a bench of a write) it may not change the routing keys: a
 *     write must reach the PLC whose scratch the profile declares;
 *   - every other key stays allowed (timeouts, `messageWait`, `monitoringTimer`, `series`,
 *     `commandSet`, `blockNo`, `f3ShortResponseHasSum`, ...), and so do the routing keys on a
 *     step that only reads (catalogue G7-04, G8-Q5). A key that is written but keeps the
 *     profile's value changes nothing and is not refused.
 *
 * OP fields (write and read steps inline; inside "request" and "then"):
 *   device    required device reference: "D@s", "D@s+10", "M@s16", "M@s16+3", "D@end",
 *             "D@end+1", a literal "D100", "SB" / "SW" (the profile's special bit / register,
 *             "SB+16"), "@scan" (the profile's scanTimeDevice). "@s" is the first number of the
 *             first scratch range of the type, "@s16" the first multiple of 16 inside it, "@end"
 *             the profile's deviceEnd. An offset ("+10", "-1") is a plain decimal number added
 *             to the resolved device number, also on a hexadecimal device: with W100-W1FF as
 *             scratch, "W@s+10" is W10A (100H + 10 decimal), not W110. A result below zero is
 *             an error.
 *   count     integer, or a limit name with an offset: "Wmax", "Wmax+40", "BRmax", "BWmax",
 *             "WBRmax", "WBWmax", "min(BRmax,BWmax)". Default: the number of values, or 1.
 *   unit      "bit" or "word"; default by the device kind. "word" on a bit device reads or
 *             writes 16 points per word.
 *   values    write only: an array (numbers, "0x1234" or "1234H" strings, {"f64":1.5},
 *             {"f32":1.5}, {"i32":-2}, {"u32":7}; a bit write takes 0/1 or a string "1100"), or
 *             {"gen":"index","mul":257} (word k = k*mul), {"gen":"alt","first":1} (bits
 *             1,0,1,0..), {"gen":"fill","value":N}.
 *   readBack  write only: read the same points back and expect the written values.
 *   metaKey   read only: keep the first word of the response under this run.meta key.
 *
 * Kind-specific keys:
 *   mutate    "request": OP fields (kind write|read); "edits": [ {"setByte":{"at":0,"value":
 *             "0x51"}}, {"setNibble":{"at":-1,"nibble":"low","value":1}}, {"append":"30"},
 *             {"truncate":1}, {"replaceSum":{"value":"0x00"}} or {"replaceSum":{"add":1}} ]
 *             ("at" counts from the end when negative; the frame is the one McProtocol encodes
 *             for the effective frame); "readOnly": true when the safety gate's mock cannot
 *             decode the edited frame (never for a mutate whose base request is a write: the
 *             gate refuses it); "recover": "none" | "reconnect" | "eot".
 *   raw       "hex": the frame ("04 0D", "{EOT}" = EOT, plus CR LF in format 4), or
 *             "requests": [ OP, ... ] (kind read|write): the frames McProtocol encodes for them,
 *             sent as ONE write (a pipelining probe); "readOnly" and "recover" as for mutate.
 *   Every command in the bytes of a mutate or raw frame must be a read, or a write that the
 *   gate's mock decodes and that lies in scratch; anything else is refused, `readOnly` or not.
 *   A readOnly frame carries read commands only, and one the mock cannot fully decode needs
 *   "recover": "reconnect" on Ethernet or "eot" on a serial line, so that no later byte can
 *   complete it or be swallowed by it (see safety_gate.h).
 *   poll      "heartbeat": device reference (optional); "subscribe": [
 *             {"name":"d","device": "D@s","count":64,"input":false} ]; "rounds": N;
 *             "bitsAsWords": true|false (default: the profile's session); "actions": [
 *             {"after":2, "write":OP}, {"after":4,"subscribe":SUB},
 *             {"after":4,"unsubscribe":"name"}, {"after":3,"prompt":"text"} ] (run in the order
 *             of "after", whatever order they are listed in; actions of one round keep their
 *             listed order). "input": true marks a read-only subscription of points outside
 *             scratch (X0, TN0): a subscription cannot write, so only ranges without it must
 *             lie in scratch.
 *   bench     "request": OP fields, or "pollSet": id of a poll step; "reps", "warmup".
 */
#pragma once

#include "hil_capture/json_reader.h"
#include "hil_capture/profile.h"
#include "mc/core/frame_config.h"
#include "mc/core/limits.h"
#include "mc/core/request.h"
#include "mc/core/types.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <optional>

namespace mc::hil {

/// @brief What a step does.
enum class StepKind : uint8_t { Write, Read, Poll, Mutate, Raw, Bench };

/// @brief What an operation is expected to produce.
enum class ExpectKind : uint8_t { Ok, PlcError, Timeout, NoResponse, NotSent, Record };

/// @brief After a mutate or raw step: how the tool brings the link back before going on.
enum class Recover : uint8_t { None, Reconnect, Eot };

/// @brief A device reference as written in a plan, parsed but not yet resolved.
struct DeviceRef {
    /// How the number is found.
    enum class Base : uint8_t {
        Literal,
        ScratchStart,
        ScratchAligned,
        End,
        SpecialBit,
        SpecialWord,
        ScanTime
    };
    Base base{Base::Literal};       ///< Which form.
    DeviceType type{DeviceType::D}; ///< Device type (Literal, ScratchStart, ScratchAligned, End).
    uint32_t number{0};             ///< Literal number.
    uint32_t align{1};              ///< ScratchAligned: align the first scratch number up to this.
    int64_t offset{0};              ///< Added to the base number.
    QString text; ///< As written; may hold "{T}" until a foreach expansion substitutes it.
    bool hasTemplate{false}; ///< True while `text` still holds "{T}".
};

/// @brief The named limits of the catalogue ("Wmax" ...).
enum class Limit : uint8_t { None, Wmax, BRmax, BWmax, WBRmax, WBWmax };

/// @brief A point count: a number or a limit with an offset, optionally min() of two limits.
struct CountExpr {
    bool present{false};  ///< False: derive from the values, else 1.
    Limit a{Limit::None}; ///< First limit; None = the plain number `value`.
    Limit b{Limit::None}; ///< Second limit of min(a, b).
    bool isMin{false};    ///< min(a, b).
    int64_t value{0};     ///< The number when `a == None`.
    int64_t delta{0};     ///< Offset added to a limit.
    QString text;         ///< As written.
};

/// @brief The values of a write, before the count is known.
struct ValuesSpec {
    /// Where the values come from.
    enum class Mode : uint8_t { None, List, Index, Alternating, Fill };
    Mode mode{Mode::None};  ///< Which form.
    QVector<uint16_t> list; ///< List: one entry per word, or per bit.
    uint32_t mul{1};        ///< Index: word k = k * mul.
    uint32_t first{1};      ///< Alternating: the first bit.
    uint32_t fill{0};       ///< Fill: the value.
};

/// @brief What the outcome of an operation is judged against.
struct Expect {
    ExpectKind kind{ExpectKind::Ok}; ///< Required outcome.
    bool hasValues{false};           ///< `values` is meaningful.
    QVector<uint16_t> values;        ///< Expected words (or bits, one 0/1 per entry).
    bool hasBitsOn{false};           ///< `bitsOn` is meaningful.
    QVector<int> bitsOn;             ///< Indexes of the bits that must be 1; all others 0.
    QString valuesFrom;              ///< Id of a write step whose values the payload starts with.
    int frames{-1};                  ///< Frames the operation must put on the wire; -1 = any.
    bool explicitKind{false};        ///< The plan named a kind (else the default applies).
};

/// @brief One read or write operation of a step.
struct OpSpec {
    bool write{false};     ///< Write rather than read.
    DeviceRef device;      ///< Head device.
    CountExpr count;       ///< Number of points or words.
    QString unit;          ///< "", "bit" or "word".
    ValuesSpec values;     ///< Write payload.
    bool readBack{false};  ///< Write: read back and compare.
    QString metaKey;       ///< Read: run.meta key for the first word.
    Expect expect;         ///< Judged outcome.
    bool hasExpect{false}; ///< The plan gave an expectation.
};

/// @brief One byte edit of a mutate step.
struct Edit {
    /// Which edit.
    enum class Kind : uint8_t { SetByte, SetNibble, Append, Truncate, ReplaceSum };
    Kind kind{Kind::SetByte}; ///< Which edit.
    int64_t at{0};            ///< SetByte, SetNibble: byte index, negative from the end.
    uint32_t value{0};        ///< SetByte, SetNibble, ReplaceSum: the new value.
    bool high{false};         ///< SetNibble: the high nibble.
    bool relative{false};     ///< ReplaceSum: `value` is added to the SUM instead of replacing it.
    QByteArray bytes;         ///< Append: bytes to add.
    int64_t count{0};         ///< Truncate: bytes to remove from the end.
};

/// @brief One subscription of a poll step.
struct PollSub {
    QString name;      ///< Name the plan refers to it by.
    DeviceRef device;  ///< Head device.
    CountExpr count;   ///< Number of points.
    bool input{false}; ///< Read-only points outside scratch.
};

/// @brief One action of a poll step, between rounds.
struct PollAction {
    /// Which action.
    enum class Kind : uint8_t { Write, Subscribe, Unsubscribe, Prompt };
    Kind kind{Kind::Write}; ///< Which action.
    int after{1};           ///< Runs after this round has finished.
    OpSpec write;           ///< Write.
    PollSub sub;            ///< Subscribe.
    QString name;           ///< Unsubscribe: subscription name.
    QString text;           ///< Prompt: text shown to the operator.
};

/// @brief The poll step's own keys.
struct PollSpec {
    bool hasHeartbeat{false};        ///< A heartbeat device was given.
    DeviceRef heartbeat;             ///< Heartbeat bit.
    QVector<PollSub> subs;           ///< Initial subscriptions.
    int rounds{1};                   ///< Rounds to run.
    std::optional<bool> bitsAsWords; ///< Overrides the session's plan option for this step.
    QVector<PollAction> actions;     ///< Between-round actions, sorted by `after` at load.
};

/// @brief The bench step's own keys.
struct BenchSpec {
    bool hasRequest{false}; ///< `request` was given (else `pollSet`).
    OpSpec request;         ///< The request repeated.
    QString pollSet;        ///< Id of a poll step whose subscription set is one repetition.
    int reps{-1};           ///< Repetitions; -1 = the command line's `--bench-reps`.
    int warmup{1};          ///< Warm-up repetitions discarded.
};

/// @brief One step of a plan as written.
struct Step {
    QString id;                     ///< Catalogue id.
    QString group;                  ///< Part of the id before the first '-'.
    QString title;                  ///< Free text.
    StepKind kind{StepKind::Read};  ///< What it does.
    QStringList conditions;         ///< Conditions (see the schema).
    QString expandOver;             ///< "", "supports" or "other".
    QJsonObject frameOverride;      ///< FrameConfig keys.
    bool scratchSkip{false};        ///< scratchTooSmall == "skip".
    QString mirrors;                ///< Appendix A vector names.
    bool readOnly{false};           ///< Mutate/raw: the gate's mock cannot decode the frame.
    Recover recover{Recover::None}; ///< Mutate/raw recovery.
    OpSpec op;                      ///< Write, read, mutate base request.
    QVector<OpSpec> then;           ///< Operations after the first one.
    QVector<Edit> edits;            ///< Mutate edits.
    QString rawHex;                 ///< Raw frame text.
    QVector<OpSpec> rawRequests;    ///< Raw: requests encoded and sent as one write.
    PollSpec poll;                  ///< Poll.
    BenchSpec bench;                ///< Bench.
    Expect expect;                  ///< Mutate/raw/bench expectation (write/read use `op.expect`).
    bool hasExpect{false};          ///< The plan gave an expectation for the step.
};

/// @brief A loaded plan.
struct Plan {
    QString id;          ///< Plan id.
    QString title;       ///< Free text.
    QString family;      ///< "qna-ethernet", "a1e", "qna-serial" or "a1c"; may be empty.
    QVector<Step> steps; ///< Steps in file order.
};

/// @brief Outcome of loading a plan.
struct PlanLoad {
    std::optional<Plan> plan; ///< Set on success.
    LoadError error;          ///< Set on failure.
    /// @brief Whether loading succeeded.
    bool ok() const { return plan.has_value(); }
};

/// @brief Loads a plan from parsed JSON.
PlanLoad loadPlan(const QJsonObject& root);

/// @brief Reads, parses and loads a plan file.
PlanLoad loadPlanFile(const QString& path);

/// @brief Checks the spelling of a requires condition ("supports:D", "!scanTime").
/// @param[in] text The condition.
/// @param[out] why What is wrong.
bool checkCondition(const QString& text, QString& why);

/// @brief Parses a device reference ("D@s+10", "M@s16", "D@end+1", "D100", "SB", "@scan").
/// @param[in] text The reference.
/// @param[out] out The parsed form.
/// @param[out] why Why it failed.
bool parseDeviceRef(const QString& text, DeviceRef& out, QString& why);

} // namespace mc::hil
