// McProtocol dispatch by FrameType, and Parser's own state machine (spec sections 5, 7.2). Only
// F3E (both DataCode::Binary and, since T-018, DataCode::Ascii) is implemented; every other
// frame family fails with UnsupportedCommand rather than reaching any frame-specific code, so
// extending isImplemented() and each dispatch site below is what later tasks (1E, 3C, 1C) do;
// nothing here needs to change shape to add a branch.
#include "mc/core/protocol.h"

#include "field_codec.h"
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

bool isImplemented(const FrameConfig& cfg) noexcept { return cfg.frame == FrameType::F3E; }

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
        return Expected<size_t>(
            detail::frame3eEncodedSize<detail::BinaryCodec>(r, m_config.series));
    }
    return Expected<size_t>(detail::frame3eEncodedSize<detail::AsciiCodec>(r, m_config.series));
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
        return detail::frame3eEncode<detail::BinaryCodec>(r, m_config, out);
    }
    return detail::frame3eEncode<detail::AsciiCodec>(r, m_config, out);
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
        return detail::frame3eMaxResponseSize<detail::BinaryCodec>(r);
    }
    return detail::frame3eMaxResponseSize<detail::AsciiCodec>(r);
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
            ? detail::frame3eTryParse<detail::BinaryCodec>(buffer, r, m_cfg, frameLength, err)
            : detail::frame3eTryParse<detail::AsciiCodec>(buffer, r, m_cfg, frameLength, err);
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
        size_t headerSize = detail::frame3eHeaderSize<detail::BinaryCodec>();
        size_t endCodeSize = detail::BinaryCodec::u16Size();
        size_t wireSize = detail::qnaResponseDataSize<detail::BinaryCodec>(r);
        ByteView wireData{buffer.data + headerSize + endCodeSize, wireSize};
        return detail::qnaResponseData<detail::BinaryCodec>(r, wireData, out);
    }
    size_t headerSize = detail::frame3eHeaderSize<detail::AsciiCodec>();
    size_t endCodeSize = detail::AsciiCodec::u16Size();
    size_t wireSize = detail::qnaResponseDataSize<detail::AsciiCodec>(r);
    ByteView wireData{buffer.data + headerSize + endCodeSize, wireSize};
    return detail::qnaResponseData<detail::AsciiCodec>(r, wireData, out);
}

const Error& Parser::error() const noexcept { return m_error; }

void Parser::reset() noexcept {
    m_status = ParseStatus::NeedMore;
    m_error = Error{};
    m_frameLength = 0;
    m_skipped = 0;
}

} // namespace mc
