/**
 * @file frame_decoder.h
 * @brief `FrameDecoder`: turns the chunks of one link into frame boundaries and a short text (op,
 * device, count, outcome) for the frame trace. Uses public library API only (`MockPlc` to read a
 * request, `McProtocol::parser()` to read the answer).
 */
#pragma once

#include "mc/core/frame_config.h"
#include "mc/core/request.h"
#include "mc_workbench/runner_types.h"

#include <QByteArray>

#include <memory>
#include <optional>

namespace mc {
class MockPlc;
} // namespace mc

namespace mc::workbench {

/**
 * @brief Decodes the chunks of one link, in the order they happened.
 *
 * A request chunk (tx) is read by a private `MockPlc` for the frame family, which also copes with a
 * request split across several chunks; the answer chunks (rx) that follow are fed to the parser of
 * that request. The state of the stream is therefore kept between calls: use one decoder per link
 * and call annotate() for every chunk in order.
 *
 * @note Runner thread only (as the object that owns it). Allocates; not meant for the library's
 *       zero-allocation paths.
 */
class FrameDecoder {
public:
    /**
     * @brief A decoder for the frame family of @p frame.
     * @param[in] frame The link's frame configuration.
     */
    explicit FrameDecoder(const mc::FrameConfig& frame);

    /// @brief Deletes the private mock.
    ~FrameDecoder();

    FrameDecoder(const FrameDecoder&) = delete;
    FrameDecoder& operator=(const FrameDecoder&) = delete;

    /**
     * @brief Fills `edge` and `note` of @p record from its bytes and the chunks seen before.
     * @param[in,out] record The next chunk of the link.
     */
    void annotate(FrameRecord& record);

private:
    void annotateRequest(FrameRecord& record);
    void annotateAnswer(FrameRecord& record);

    mc::FrameConfig m_frame;
    std::unique_ptr<mc::MockPlc> m_mock;
    std::optional<mc::Op> m_op;       ///< The request the next answer belongs to.
    mc::Device m_head{};
    quint16 m_count{0};
    QByteArray m_answer;              ///< Answer bytes of the current request so far.
    quint32 m_eotSeen{0};
};

} // namespace mc::workbench
