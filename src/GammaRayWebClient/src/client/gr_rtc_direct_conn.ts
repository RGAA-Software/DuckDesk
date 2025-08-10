import { GrConn } from '@/client/gr_conn.ts'
import { GrSdk } from '@/client/gr_sdk.ts'
import { GrConnParams } from '@/client/gr_sdk_params.ts'
import { GrRendererManager } from '@/renderer/gr_renderer_manager.ts'
import axios, { Axios, type AxiosResponse } from 'axios'
import { GrResponse } from '@/base/gr_response.ts'

export class GrRtcDirectConn extends GrConn {

    // peer connection
    rtcPeerConn: RTCPeerConnection;
    // data channel
    rtcDataChannel: RTCDataChannel;

    constructor(sdk: GrSdk, params: GrConnParams, rendererManager: GrRendererManager) {
        super(sdk, params, rendererManager)
    }

    async start() {
        await super.start();

        const configs = {
            optional: [
            {
                DtlsSrtpKeyAgreement: true
            },
            {
                RtpDataChannels: true
            }
        ]};
        this.rtcPeerConn = new RTCPeerConnection(configs);
        this.rtcPeerConn.onicecandidate = (ev: RTCPeerConnectionIceEvent)=> {
            if (ev.candidate == null) {
                console.log("ice don't have candidate")
                return;
            }
            console.log("onicecandidate, ", ev);
        }
        this.rtcPeerConn.onicegatheringstatechange = (ev: Event)=> {
            console.log("ICE Gather, ", ev);
        }
        this.rtcPeerConn.onconnectionstatechange = (ev: Event)=> {
            console.log("Connection State, ", ev);
        }
        this.rtcPeerConn.ontrack = (ev: RTCTrackEvent)=> {
            console.log("ontrack, ", ev);
            if (ev.track.kind == "video" && this.grRendererManager.rendererCanvas != undefined) {
                this.grRendererManager.remoteVideoElement.srcObject = ev.streams[0];
                console.log("ontrack, ", ev);
            }
        }

        // data channel
        this.rtcDataChannel = this.rtcPeerConn.createDataChannel("sendChannel");
        this.rtcDataChannel.onopen = (ev: Event) => {
            if (this.rtcDataChannel == undefined) {
                return;
            }
            console.log("data channel, onopen", ev);
            const state = this.rtcDataChannel.readyState;
            if (state === "open") {
                console.log("data channel open");
            } else {
                console.log("data channel: " + state);
            }
        }
        this.rtcDataChannel.onclose = (ev: Event) => {
            console.log("data channel, onclose", ev);
        }

        //
        // create offer
        const offer = await this.rtcPeerConn.createOffer({
            offerToReceiveAudio: true,
            offerToReceiveVideo: true,
        });
        console.log("offer: ", offer);

        await this.rtcPeerConn.setLocalDescription(offer);

        const deviceId = "1000";
        const streamId = "2000";
        const body = {
            "sdp": offer.sdp,
        };
        const allocResult = await this.allocRemoteLocalRtc(deviceId, streamId, body);
        if (allocResult == null) {
            return;
        }
        console.log("allocResult: ", allocResult);

        if (allocResult.code != 200) {
            console.log("allocResult failed");
            return;
        }

        const answerSdp = allocResult.data.answer_sdp;
        await this.rtcPeerConn.setRemoteDescription({
            "sdp": answerSdp,
            "type": "answer",
        });
    }

    async stop() {
        await super.stop()
    }

    async allocRemoteLocalRtc(deviceId: string, streamId: string, body: { [key: string]: any }) {
        try {
            const url = `/api/alloc/local/rtc?device_id=${deviceId}&stream_id=${streamId}`;
            console.log("request url", url);
            const response: AxiosResponse<GrResponse> = await axios.post(url, body);
            return response.data;
        } catch (error) {
            console.error("resp error:", error);
            return null;
        }
    }

}
