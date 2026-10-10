#include "encoded_video_delivery.h"

#include <limits>
#include <utility>

namespace px {

EncodedVideoDelivery MakeReassembledVideoDelivery(const media::VideoFrame& frame) {
    auto video_message = std::make_shared<Message>();
    video_message->set_type(kVideoFrame);
    auto& video = *video_message->mutable_video_frame();
    video.set_type(frame.codec == media::VideoCodec::kH265 ? kNetHevc : kNetH264);
    video.set_data(frame.encoded.data(), frame.encoded.size());
    video.set_frame_index(frame.frame_index);
    video.set_key(frame.kind == media::VideoFrameKind::kIdr);
    video.set_frame_width(frame.width);
    video.set_frame_height(frame.height);
    video.set_mon_name(frame.monitor);
    video.set_mon_index(frame.stream);
    video.set_extra("udp_synth");
    return {.message = std::move(video_message),
            .dependency = VideoReferenceDependency{.kind = frame.kind, .preceding_frame_index = frame.preceding_frame_index}};
}

bool RequiresVideoReferenceReset(const VideoFrame& frame, const std::optional<std::uint64_t> last_decoded_frame,
                                 const std::optional<VideoReferenceDependency>& dependency) {
    // A fresh decoder still needs the startup gate's IDR and complete configuration check.
    if (!last_decoded_frame || frame.key()) return false;
    if (dependency) {
        const bool dependent_frame =
            dependency->kind == media::VideoFrameKind::kPredicted || dependency->kind == media::VideoFrameKind::kReferenceRecovery;
        return !dependent_frame || !dependency->preceding_frame_index || *dependency->preceding_frame_index != *last_decoded_frame ||
               frame.frame_index() <= *last_decoded_frame;
    }
    return *last_decoded_frame == std::numeric_limits<std::uint64_t>::max() || frame.frame_index() != *last_decoded_frame + 1;
}

}  // namespace px
