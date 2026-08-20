
import axios, { Axios, type AxiosResponse } from 'axios'
import {PxConn} from "./px_conn.ts";
import {PxConnParams} from "./px_sdk_params.ts";
import {PxSdk} from "./px_sdk.ts";
import {PxRendererManager} from "../renderer/px_renderer_manager.ts";
import {PxResponse} from "../base/px_response.ts";

export class PxRtcDirectConn extends PxConn {

    // peer connection
    rtcPeerConn: RTCPeerConnection;
    // data channel
    rtcDataChannel: RTCDataChannel;

    constructor(sdk: PxSdk, params: PxConnParams, rendererManager: PxRendererManager) {
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
                console.log("ontrack ===>, ", ev);

                this.grRendererManager.remoteVideoElement.width = window.innerWidth;
                this.grRendererManager.remoteVideoElement.height = window.innerHeight;

                // if (this.displayMode === 'fit-width') {
                //     this.grRendererManager.remoteVideoElement.width = viewportWidth;
                //     this.grRendererManager.remoteVideoElement.height = frame.displayHeight * (viewportWidth / frame.displayWidth);
                // } else if (this.displayMode === 'fit-height') {
                //     this.renderCanvas.height = viewportHeight;
                //     this.renderCanvas.width = frame.displayWidth * (viewportHeight / frame.displayHeight);
                //
                // }

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

        const deviceId = this.grConnParams.deviceId ?? "web-user";
        const streamId = this.grConnParams.clientNonce ?? crypto.randomUUID();
        const body = {
            "sdp": offer.sdp,
            "ticket": this.grConnParams.ticket ?? "",
            "client_nonce": this.grConnParams.clientNonce ?? streamId,
            "instance_id": this.grConnParams.instanceId ?? "",
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
            const contentType = this.grConnParams.instanceId ? '&content_type=game_stream' : '';
            const url = `/api/alloc/local/rtc?device_id=${encodeURIComponent(deviceId)}&stream_id=${encodeURIComponent(streamId)}${contentType}`;
            console.log("request url", url);
            const response: AxiosResponse<PxResponse> = await axios.post(url, body);
            return response.data;
        } catch (_error) {
            // Axios errors retain the request body, including the one-time ticket.
            console.error("RTC signaling request failed");
            return null;
        }
    }

}
