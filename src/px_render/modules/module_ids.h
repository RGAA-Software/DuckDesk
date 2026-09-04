//
// Created by RGAA on 22/11/2024.
//

#ifndef PX_RENDER_MODULE_IDS_H
#define PX_RENDER_MODULE_IDS_H

#include <string>

// Stable module and transport identifiers. Values are protocol compatibility
// keys and must not change when a component's delivery model changes.

namespace px
{

    const std::string kAmfVideoEncoderId = "a159a600-9a12-46a1-8e79-cba9645600d2";
    const std::string kDdaCaptureSourceId = "0f00e864-a7dd-4b83-8921-220fbed78cc5";
    const std::string kGdiCaptureSourceId = "6c926697-5511-b138-a35b-c6ffa9cee66d";
    const std::string kFfmpegVideoEncoderId = "cbc9690d-456a-4f74-b594-44468637f3c3";
    const std::string kFrameDebuggerObserverId = "bfb3fadc-6f37-401c-a927-88c3ae2d1e95";
    const std::string kMediaRecorderSinkId = "21d1c305-e68c-4079-8a4a-d00735be609b";
    const std::string kLivePusherSinkId = "f158b253-40a9-4a4a-8fb7-2b595d9f4f6f";
    const std::string kNetWebRtcRemoteLibraryId = "4998fd5a-7cfa-4c4d-af88-8714a14f5ab5";
    const std::string kNetUdpTransportId = "00fc65ed-b824-4845-ac5a-8635bc2336a8";
    const std::string kNetWsTransportId = "711882d5-8987-4c80-826f-a783f3df9240";
    const std::string kNvencEncoderModuleId = "95ccd9a2-b277-48dd-a8cd-8c20790742d4";
    const std::string kFrameResizerProcessorId = "cd407b93-429c-44a9-9c36-3429d9b390bb";
    const std::string kWasAudioCaptureSourceId = "75f2c0ba-d76b-4955-944c-220838e7b0fe";
    const std::string kOpusEncoderModuleId = "e25954da-205e-430f-af61-99e0e200d119";
    const std::string kVrManagerModuleId = "f4a43a64-52e8-4a51-b5a1-6e9b57333b59";
    const std::string kRelayTransportId = "55b2a2fa-b4e9-4ff6-a0a4-522df80ebda2";
    const std::string kClipboardServiceId = "5b2a1187-e93b-4769-96f3-dfecf2d5d8e4";
    const std::string kFrameCarrierProcessorId = "ebde829b-ef0f-4cbc-961a-3cca5f3d646c";
    const std::string kJoystickServiceId = "102a229e-295d-444e-9ca0-b6644f3198f6";
    const std::string kEventReplayServiceId = "b6cb3d88-f397-4182-863c-2aaed752d1a9";
    const std::string kNetWebRtcLocalLibraryId = "bea7bf25-d07f-440a-acef-ac845d748958";
    const std::string kVoiceCallServiceId = "5a48bb2e-f98b-4d49-a73a-31e49ae45239";

}

#endif // PX_RENDER_MODULE_IDS_H
