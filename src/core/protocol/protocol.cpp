// McProtocol dispatch by FrameType, and Parser's own state machine (spec sections 5, 7.2). F3E
// and F1E (both DataCode::Binary and DataCode::Ascii) are implemented; every other frame family
// fails with UnsupportedCommand rather than reaching any frame-specific code, so extending
// isImplemented() and the per-frame helpers below is what later tasks (3C, 1C) do; nothing here
// needs to change shape to add a branch.
#include "mc/core/protocol.h"

#include "field_codec.h"
#include "frame_1e.h"
#include "frame_3e.h"

#include <utility>

namespace mc {
namespace {

Error unsupportedCommandError() noexcept {
    Error e{};
    e.category = ErrorCategory::Encode;
    e.code = ErrorCode::UnsupportedCommand;
    e.message = "frame type/code not implemented yet";
    return e;
}

bool isImplemented(const FrameConfig& cfg) noexcept {
    return cfg.frame == FrameType::F3E || cfg.frame == FrameType::F1E;
}

// Per-frame dispatch, one helper per operation, templated on the wire code. Only called once
// isImplemented(cfg) holds, so the two-way choice on cfg.frame is exhaustive.
template <class Codec> size_t frameEncodedSize(const Request& r, const FrameConfig& cfg) noexcept {
    return cfg.frame == FrameType::F1E ? detail::frame1eEncodedSize<Codec>(r)
                                       : detail::frame3eEncodedSize<Codec>(r, cfg.series);
}

template <class Codec>
Expected<size_t> frameEncode(const Request& r, const FrameConfig& cfg,
                             MutableByteView out) noexcept {
    return cfg.frame == FrameType::F1E ? detail::frame1eEncode<Codec>(r, cfg, out)
                                       : detail::frame3eEncode<Codec>(r, cfg, out);
}

template <class Codec>
size_t frameMaxResponseSize(const Request& r, const FrameConfig& cfg) noexcept {
    return cfg.frame == FrameType::F1E ? detail::frame1eMaxResponseSize<Codec>(r)
                                       : detail::frame3eMaxResponseSize<Codec>(r);
}

template <class Codec>
ParseStatus frameTryParse(ByteView buffer, const Request& r, const FrameConfig& cfg,
                          size_t& frameLength, Error& error) noexcept {
    return cfg.frame == FrameType::F1E
               ? detail::frame1eTryParse<Codec>(buffer, r, frameLength, error)
               : detail::frame3eTryParse<Codec>(buffer, r, cfg, frameLength, error);
}

// Decodes the response data of a frame that already parsed as Done: skips the frame's own head
// (3E: header + end code; 1E: subheader + end code) and hands the wire data to the command layer.
template <class Codec>
Expected<size_t> frameResponsePayload(ByteView buffer, const Request& r, const FrameConfig& cfg,
                                      MutableByteView out) noexcept {
    if (cfg.frame == FrameType::F1E) {
        size_t headSize = detail::frame1eResponseHeadSize<Codec>();
        ByteView wireData{buffer.data + headSize, detail::a1eResponseDataSize<Codec>(r)};
        return detail::a1eResponseData<Codec>(r, wireData, out);
    }
    size_t headSize = detail::frame3eHeaderSize<Codec>() + Codec::u16Size();
    ByteView wireData{buffer.data + headSize, detail::qnaResponseDataSize<Codec>(r)};
    return detail::qnaResponseData<Codec>(r, wireData, out);
}

// Frame-independent (module spec "Payload contract"): every frame family normalizes a response
// the same way, so this is not part of the FrameType dispatch below.
size_t normalizedPayloadSize(const Request& r) noexcept {
    if (r.isWrite()) {
        return 0;
    }
    if (!r.isBitOp()) {
        return static_cast<size_t>(r.count) * 2;
    }
    if (r.bitLayout == BitLayout::BytePerPoint) {
        return r.count;
    }
    return (static_cast<size_t>(r.count) + 7) / 8;
}

} // namespace

McProtocol::McProtocol(const FrameConfig& cfg) noexcept : m_config(cfg) {}

const FrameConfig& McProtocol::config() const noexcept { return m_config; }

Expected<size_t> McProtocol::encodedSize(const Request& r) const noexcept {
    auto validated = validate(r, m_config);
    if (!validated.hasValue()) {
        return Expected<size_t>(validated.error());
    }
    if (!isImplemented(m_config)) {
        return Expected<size_t>(unsupportedCommandError());
    }
    if (m_config.code == DataCode::Binary) {
        return Expected<size_t>(frameEncodedSize<detail::BinaryCodec>(r, m_config));
    }
    return Expected<size_t>(frameEncodedSize<detail::AsciiCodec>(r, m_config));
}

Expected<size_t> McProtocol::encode(const Request& r, MutableByteView out) const noexcept {
    auto validated = validate(r, m_config);
    if (!validated.hasValue()) {
        return Expected<size_t>(validated.error());
    }
    if (!isImplemented(m_config)) {
        return Expected<size_t>(unsupportedCommandError());
    }
    if (m_config.code == DataCode::Binary) {
        return frameEncode<detail::BinaryCodec>(r, m_config, out);
    }
    return frameEncode<detail::AsciiCodec>(r, m_config, out);
}

Expected<ByteBuf> McProtocol::encode(const Request& r) const {
    auto sizeResult = encodedSize(r);
    if (!sizeResult.hasValue()) {
        return Expected<ByteBuf>(sizeResult.error());
    }
    ByteBuf buf(sizeResult.value());
    auto encodeResult = encode(r, MutableByteView{buf.data(), buf.size()});
    if (!encodeResult.hasValue()) {
        return Expected<ByteBuf>(encodeResult.error());
    }
    return Expected<ByteBuf>(std::move(buf));
}

size_t McProtocol::maxResponseSize(const Request& r) const noexcept {
    if (!isImplemented(m_config)) {
        return 0;
    }
    if (m_config.code == DataCode::Binary) {
        return frameMaxResponseSize<detail::BinaryCodec>(r, m_config);
    }
    return frameMaxResponseSize<detail::AsciiCodec>(r, m_config);
}

size_t McProtocol::payloadSize(const Request& r) const noexcept { return normalizedPayloadSize(r); }

Parser McProtocol::parser(const Request& r) const noexcept {
    Parser p;
    p.m_cfg = m_config;
    p.m_op = r.op;
    p.m_count = r.count;
    p.m_bitLayout = r.bitLayout;
    p.m_status = ParseStatus::NeedMore;
    p.m_error = Error{};
    p.m_frameLength = 0;
    p.m_skipped = 0;
    return p;
}

ParseStatus Parser::feed(ByteView buffer) noexcept {
    if (m_status != ParseStatus::NeedMore) {
        return m_status; // Done/Failed are sticky until reset(); see class doc.
    }
    if (!isImplemented(m_cfg)) {
        m_status = ParseStatus::Failed;
        m_error = unsupportedCommandError();
        m_frameLength = 0;
        return m_status;
    }

    Request r{};
    r.op = m_op;
    r.count = m_count;
    r.bitLayout = m_bitLayout;

    size_t frameLength = 0;
    Error err{};
    ParseStatus status =
        (m_cfg.code == DataCode::Binary)
            ? frameTryParse<detail::BinaryCodec>(buffer, r, m_cfg, frameLength, err)
            : frameTryParse<detail::AsciiCodec>(buffer, r, m_cfg, frameLength, err);
    if (status == ParseStatus::NeedMore) {
        return ParseStatus::NeedMore; // Nothing cached; a later, longer buffer starts over.
    }
    m_status = status;
    m_frameLength = frameLength;
    m_error = err;
    return m_status;
}

size_t Parser::frameLength() const noexcept { return m_frameLength; }

size_t Parser::skipped() const noexcept { return m_skipped; }

Expected<size_t> Parser::payload(ByteView buffer, MutableByteView out) const noexcept {
    if (!isImplemented(m_cfg)) {
        return Expected<size_t>(unsupportedCommandError());
    }

    Request r{};
    r.op = m_op;
    r.count = m_count;
    r.bitLayout = m_bitLayout;

    size_t needed = normalizedPayloadSize(r);
    if (out.size < needed) {
        return Expected<size_t>(detail::fieldBufferTooSmallError());
    }

    if (m_cfg.code == DataCode::Binary) {
        return frameResponsePayload<detail::BinaryCodec>(buffer, r, m_cfg, out);
    }
    return frameResponsePayload<detail::AsciiCodec>(buffer, r, m_cfg, out);
}

const Error& Parser::error() const noexcept { return m_error; }

void Parser::reset() noexcept {
    m_status = ParseStatus::NeedMore;
    m_error = Error{};
    m_frameLength = 0;
    m_skipped = 0;
}

} // namespace mc
