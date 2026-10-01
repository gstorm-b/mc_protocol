/**
 * @file mock_plc.h
 * @brief A PLC responder in standard C++: request bytes in, response bytes out, with a device
 * memory image and fault injection, for tests and demos.
 */
#pragma once

#include "mc/core/device.h"
#include "mc/core/frame_config.h"
#include "mc/core/request.h"
#include "mc/core/result.h"
#include "mc/core/types.h"

#include <cstdint>
#include <initializer_list>
#include <memory>
#include <vector>

namespace mc {

/**
 * @struct MockOptions
 * @brief Error codes the mock answers with when a test does not choose one.
 *
 * These are TEST VALUES: where a golden vector carries an error code it is reused (C051H, 50H,
 * 5BH + 10H, 7151H, 06H); C059H is an arbitrary choice. The library asserts nothing about their
 * meaning (spec `mc-protocol-frame-spec.md` §7.2).
 *
 * @see MockPlc
 */
struct MockOptions {
    uint16_t unsupportedQna{0xC059}; ///< 3E / 3C: command or subcommand the mock does not support.
    uint8_t unsupported1e{0x50};     ///< 1E end code for an unsupported command (as V-1E-B-12).
    uint8_t unsupported1c{0x06};     ///< 1C NAK code for an unsupported command (as V-1C1-08).
    uint16_t sumErrorQna{0x7151};    ///< 3C request with a wrong SUM (code as in V-3C1-05).
    uint8_t sumError1c{0x06};        ///< 1C request with a wrong SUM.
    uint16_t outOfRangeQna{0xC051};  ///< 3E / 3C: device beyond setDeviceLimit() (as V-3E-B-10).
    uint8_t outOfRange1e{0x5B};      ///< 1E end code for a device beyond the limit (as V-1E-B-11).
    uint8_t outOfRange1eAbnormal{0x10}; ///< 1E abnormal code sent with outOfRange1e (as V-1E-B-11).
    uint8_t outOfRange1c{0x06};      ///< 1C NAK code for a device beyond the limit.
};

/**
 * @enum Corruption
 * @brief Ways to damage the next response(s), for protocol-error tests.
 *
 * On an Ethernet frame the modes act as follows. WrongRoute adds 1 to the network, PC and station
 * fields of the response header (wrapping at FFH), not to the route echoed in error information.
 * WrongSumCheck and WrongBlockNo are serial-only: on an Ethernet frame they leave the response
 * unchanged. The ASCII code changes the same fields, in their text form. A 1E response has no
 * network, PC or station field, so WrongRoute leaves it unchanged; the other modes act on a 1E
 * response as they do on a 3E one.
 *
 * On a serial frame (3C, 1C) WrongSubheader leaves the response unchanged. WrongSumCheck adds 1
 * to the SUM (wrapping at FFH); a response without a SUM (ACK and NAK, a format 3 short response
 * unless FrameConfig::f3ShortResponseHasSum, sum check off) is left unchanged. WrongRoute adds 1
 * to the station, network and PC fields of the access route on 3C and to the station and PC
 * fields on 1C (wrapping at FFH; the self-station is not touched). WrongBlockNo adds 1 to the
 * block number of format 2 (wrapping at FFH) and leaves the other formats unchanged. WrongRoute
 * and WrongBlockNo recompute the SUM of a response that has one, so the named field is the only
 * damage; only WrongSumCheck leaves a SUM that does not match. Truncate drops the last byte (the
 * LF in format 4), JunkPrefix puts three 55H bytes in front of the frame and ExtraByte one 00H
 * byte after it (after the CR LF in format 4). The ASCII digits of a changed field are
 * upper-case hexadecimal.
 *
 * @see MockPlc::corruptNext
 */
enum class Corruption : uint8_t {
    WrongSubheader, ///< 3E/1E: first byte XOR 01H (ASCII: first character changed to 'E').
    WrongSumCheck,  ///< 3C/1C: SUM value + 1.
    WrongRoute,     ///< Station / network / PC field + 1.
    WrongBlockNo,   ///< Format 2: block number + 1.
    Truncate,       ///< Last byte dropped (the client must time out).
    JunkPrefix,     ///< Three 55H bytes before the frame (serial: must be skipped).
    ExtraByte       ///< One 00H byte after the frame (Ethernet: stream desync).
};

/**
 * @struct MockRequestRecord
 * @brief One decoded request, for assertions.
 * @see MockPlc::requests
 */
struct MockRequestRecord {
    FrameType frame{FrameType::F3E}; ///< Frame family the mock was configured for.
    Op op{Op::ReadWords};            ///< Operation of the request.
    Device head{};                   ///< First point of the request.
    uint16_t count{0};               ///< Points: bits for bit ops, words for word ops.
    PlcSeries series{PlcSeries::QL}; ///< From the subcommand actually received (QnA), else QL.
    bool answered{false};            ///< false when muted, station mismatch, or dropped.
    Error answeredWith{};            ///< Ok, or the PLC error the mock returned.
};

/**
 * @class MockPlc
 * @brief A sans-I/O PLC responder with a device memory image and fault injection.
 *
 * Speaks one frame family, code, format, sum-check and station setting: those of the
 * FrameConfig it is constructed from (the same one the client uses). Requests arrive through
 * bytesIn() in any fragmentation; every complete request is executed at once and its response
 * is queued for nextResponse(). The mock performs no I/O, reads no clock, starts no thread and
 * throws nothing; the same inputs always produce the same bytes. It is test and demo
 * infrastructure and may allocate freely.
 *
 * In this version FrameType::F3E and FrameType::F1E (Binary and ASCII each) and FrameType::F3C
 * and FrameType::F1C (ASCII, formats 1 to 4) are answered. A 1E request is framed by its
 * command's fixed layout plus the point count (reference spec §4.2, §5.3); commands 00H to 03H
 * are executed, 04H and 05H are answered with MockOptions::unsupported1e. The constructor accepts
 * the other frame families, but bytesIn() then recognises no request: nothing is queued for
 * nextResponse() and nothing is added to requests().
 *
 * A serial request is received as the PLC side of reference spec §6.3 does:
 *  - Bytes before the start byte (ENQ; STX in format 3) are skipped without a trace: they add
 *    nothing to requests().
 *  - Formats 1, 2 and 4 have no terminator before the SUM, so a request ends where its command
 *    and point count say (§4.1, §4.3); format 3 ends at ETX. A format 1, 2 or 4 request whose
 *    layout cannot be followed (a command the mock has no layout for, a route that is not
 *    hexadecimal) is skipped like junk: its start byte is dropped and the scan goes on to the
 *    next one. A format 3 request is always framed; one with an unknown command is answered with
 *    an error.
 *  - 3C: commands 0401 and 1401 are executed (subcommands 0000 to 0003, so Q/L and iQ-R device
 *    fields); 0403 and 1402 are framed and answered with MockOptions::unsupportedQna. 1C: BR, WR,
 *    BW, WW (ACPU) and JR, QR, JW, QW (AnA/AnU) are executed, the message wait character is
 *    accepted and ignored; BT, WT, JT, QT are framed and answered with MockOptions::unsupported1c.
 *  - EOT (format 4: EOT CR LF) at any point drops the partial request and is counted by
 *    eotCount(), also while muted. It is not answered.
 *  - A request whose station number differs from FrameConfig::stationNo is added to requests()
 *    with `answered == false`, is not executed and gets no response; it does not use up a
 *    muteNext(). Another network, PC or self-station number is answered, and echoed.
 *  - With FrameConfig::sumCheck a request whose SUM is wrong is not executed: it is answered with
 *    NAK (QNAK in format 3; NN on 1C) and MockOptions::sumErrorQna or sumError1c. Without
 *    sumCheck neither a request nor a response carries a SUM.
 *  - A response echoes the access route and, in format 2, the block number of its request.
 *    The "no data" and error responses of format 3 carry a SUM only when
 *    FrameConfig::f3ShortResponseHasSum is set (spec §10 Q1); ACK and NAK carry none.
 *
 * Bit devices hold single bits. Word access to a bit device sees bit i of word k at
 * head + 16k + i (spec §2.4). Unwritten memory reads as 0. Device numbers are not aliased
 * (M9000 on 1E is M9000, not SM1000).
 *
 * @note Not thread-safe: use one object from one thread at a time.
 * @see MockOptions, MockRequestRecord, Corruption
 */
class MockPlc {
public:
    /**
     * @brief Creates a mock speaking exactly the settings of @p cfg.
     * @param[in] cfg Frame, code, format, sum-check and station settings to speak.
     * @param[in] opt Error codes to answer with when a test does not choose one.
     * @pre `cfg.validate()` is Ok.
     */
    explicit MockPlc(const FrameConfig& cfg, const MockOptions& opt = {});

    /// @brief Destroys the mock and its memory image.
    ~MockPlc();

    MockPlc(const MockPlc&) = delete;
    MockPlc& operator=(const MockPlc&) = delete;

    /**
     * @brief Moves the mock, with its memory, log and pending responses.
     * @param[in,out] other Source; must not be used afterwards except to destroy or assign it.
     */
    MockPlc(MockPlc&& other) noexcept;

    /**
     * @brief Move-assigns the mock, replacing this one's state.
     * @param[in,out] other Source; must not be used afterwards except to destroy or assign it.
     * @return This object.
     */
    MockPlc& operator=(MockPlc&& other) noexcept;

    /// @name Bytes (sans-I/O)
    /// @{

    /**
     * @brief Feeds request bytes received from the client.
     *
     * Any fragmentation is accepted, and several requests in one buffer are all handled. Bytes
     * that cannot start a request are dropped (see the frame family's decoder).
     *
     * @param[in] bytes Bytes received, in order.
     * @post Each complete request has been executed against the memory image, added to
     * requests(), and, unless muted, its response is available through nextResponse().
     */
    void bytesIn(ByteView bytes);

    /**
     * @brief Takes the next response to send, if any.
     *
     * @param[out] out The response bytes; valid until the next call on this object.
     * @return true when a response was returned in @p out, false when none is pending.
     */
    bool nextResponse(ByteView& out);
    /// @}

    /// @name Memory image
    /// @{

    /**
     * @brief Sets one word.
     *
     * On a bit device this sets the 16 points head..head+15 from the bits of @p v (bit 0 to
     * @p d itself).
     *
     * @param[in] d Word device, or the first point of a bit device's 16-point word.
     * @param[in] v New value.
     */
    void setWord(Device d, uint16_t v);

    /**
     * @brief Sets consecutive words; word k is at d.number + k, or at d.number + 16k on a bit
     * device.
     * @param[in] head First word.
     * @param[in] values Values, in device order.
     */
    void setWords(Device head, std::initializer_list<uint16_t> values);

    /**
     * @brief Sets one point of a bit device.
     * @param[in] d Bit device point. Ignored when @p d is a word device.
     * @param[in] v New value.
     */
    void setBit(Device d, bool v);

    /**
     * @brief Sets consecutive points of a bit device.
     * @param[in] head First point. Ignored when @p head is a word device.
     * @param[in] values Values, in device order.
     */
    void setBits(Device head, std::initializer_list<bool> values);

    /**
     * @brief Reads one word; on a bit device, the 16 points starting at @p d packed into a word.
     * @param[in] d Word device, or the first point of a bit device's 16-point word.
     * @return The word, 0 where nothing was written.
     */
    uint16_t word(Device d) const;

    /**
     * @brief Reads one point of a bit device.
     * @param[in] d Bit device point.
     * @return The point, false where nothing was written or when @p d is a word device.
     */
    bool bit(Device d) const;

    /**
     * @brief Makes points with number >= @p limit not exist.
     *
     * Any request touching one gets the out-of-range error of MockOptions. Without a call, a
     * device type has no limit.
     *
     * @param[in] t Device type the limit applies to.
     * @param[in] limit First device number that does not exist.
     */
    void setDeviceLimit(DeviceType t, uint32_t limit);
    /// @}

    /// @name Faults
    /// @{

    /**
     * @brief Answers every request touching [@p first, @p last] of @p t with a PLC error.
     *
     * The answer is an end code plus error information on 3E, an end code (plus the abnormal
     * code when it is 5BH) on 1E, and NAK (QNAK in format 3) with a 4-character code on 3C or
     * NAK (NN in format 3) with a 2-character code on 1C. The 1E end code and the 1C code are one
     * byte: a larger @p code is sent as its low byte. A faulted request changes no memory.
     *
     * @param[in] t Device type the fault applies to.
     * @param[in] first First device number of the faulted range.
     * @param[in] last Last device number of the faulted range, inclusive.
     * @param[in] code PLC error code to answer with.
     * @param[in] abnormal 1E abnormal code, sent when @p code is 5BH; unused elsewhere.
     */
    void failRange(DeviceType t, uint32_t first, uint32_t last, uint16_t code,
                   uint8_t abnormal = 0);

    /**
     * @brief Removes every failRange() fault, ends mute() and muteNext(), and cancels pending
     * corruptNext() damage.
     */
    void clearFaults();

    /**
     * @brief Swallows every request while on, so the client times out.
     *
     * A swallowed request is decoded and logged (`answered == false`) but not executed: memory is
     * untouched and no response is queued.
     *
     * @param[in] on true to swallow, false to answer again.
     */
    void mute(bool on);

    /**
     * @brief Swallows the next @p n requests, as mute() does.
     * @param[in] n Number of requests to swallow.
     */
    void muteNext(uint32_t n);

    /**
     * @brief Damages the next @p n responses.
     *
     * Calls queue: a later call takes effect once the responses of the earlier ones are used up.
     * A swallowed request does not use up a response. Nothing happens when @p n is 0.
     *
     * @param[in] c How to damage them.
     * @param[in] n Number of responses to damage.
     */
    void corruptNext(Corruption c, uint32_t n = 1);
    /// @}

    /// @name Observation
    /// @{

    /**
     * @brief Every request decoded so far, in arrival order.
     * @return The request log; valid until the next call on this object that changes it.
     */
    const std::vector<MockRequestRecord>& requests() const;

    /// @return Number of EOT (or EOT CR LF) received, whatever mute() says; 0 on Ethernet frames.
    uint32_t eotCount() const;

    /// @brief Empties the request log.
    void clearLog();
    /// @}

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl; ///< All state; keeps the private headers out of this one.
};

} // namespace mc
