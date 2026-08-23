import {PxConn} from './px_conn.ts';
import {PxConnParams} from './px_sdk_params.ts';
import {PxSdk} from './px_sdk.ts';
import {PxRendererManager} from '../renderer/px_renderer_manager.ts';
import {PxProtoMsg} from '../messages/px_proto_messages.ts';

export class PxRtcConn extends PxConn {
    private pc?: RTCPeerConnection;
    private socket?: WebSocket;
    private mediaChannel?: RTCDataChannel;
    private ftChannel?: RTCDataChannel;
    private inputChannel?: RTCDataChannel;
    private roomId = '';
    private relayIndex = 0;
    private heartbeatIndex = 0;
    private heartbeatTimer = 0;
    private pendingLocalIce: RTCIceCandidate[] = [];
    private pendingRemoteIce: RTCIceCandidateInit[] = [];
    private readonly clientId = `web_${crypto.randomUUID().replace(/-/g, '')}`;

    constructor(sdk: PxSdk, params: PxConnParams, rendererManager: PxRendererManager) {
        super(sdk, params, rendererManager);
    }

    async start() {
        const p = this.grConnParams;
        if (!p.ticket || !p.clientNonce || !p.deviceId || !p.relayHost || !p.relayPort || !p.rtcIceConfig) {
            throw new Error('Standard RTC launch parameters are incomplete');
        }
        const iceServers: RTCIceServer[] = p.rtcIceConfig.ice_servers.map((server) => ({
            urls: server.urls,
            username: server.username,
            credential: server.credential,
        }));
        this.pc = new RTCPeerConnection({ iceServers, iceTransportPolicy: 'all' });
        this.pc.onicecandidate = (event) => {
            if (!event.candidate) return;
            if (!this.pc?.remoteDescription) {
                this.pendingLocalIce.push(event.candidate);
                return;
            }
            this.sendIce(event.candidate);
        };
        this.pc.onconnectionstatechange = () => {
            console.log('Standard RTC connection state:', this.pc?.connectionState);
        };
        this.pc.ontrack = (event) => {
            if (event.track.kind === 'video' && event.streams[0]) {
                this.grRendererManager.remoteVideoElement.srcObject = event.streams[0];
            }
        };

        this.mediaChannel = this.pc.createDataChannel('media_data_channel', { ordered: true });
        this.mediaChannel.binaryType = 'arraybuffer';
        this.mediaChannel.onmessage = (event) => void this.onDataMessage(event.data);
        this.ftChannel = this.pc.createDataChannel('ft_data_channel', { ordered: true });
        this.inputChannel = this.pc.createDataChannel('input_data_channel', { ordered: true });

        const scheme = window.location.protocol === 'https:' ? 'wss' : 'ws';
        const query = new URLSearchParams({
            device_id: this.clientId,
            remote_device_id: p.deviceId,
            device_name: 'WebClient',
            stream_id: p.clientNonce,
            rtc_signal: '1',
            ticket: p.ticket,
            client_nonce: p.clientNonce,
        });
        if (p.instanceId) query.set('instance_id', p.instanceId);
        this.socket = new WebSocket(`${scheme}://${p.relayHost}:${p.relayPort}/relay?${query}`);
        this.socket.binaryType = 'arraybuffer';
        this.socket.onopen = () => {
            this.sendRelay({ type: this.relayType('kRelayHello'), fromDeviceId: this.clientId, hello: {} });
            this.sendRelay({
                type: this.relayType('kRelayCreateRoom'),
                createRoom: {
                    deviceId: this.clientId,
                    remoteDeviceId: p.deviceId,
                    deviceName: 'WebClient',
                    streamId: p.clientNonce,
                },
            });
            this.heartbeatTimer = window.setInterval(() => this.sendRelay({
                type: this.relayType('kRelayHeartBeat'),
                fromDeviceId: this.clientId,
                heartbeat: { index: this.heartbeatIndex++ },
            }), 1000);
        };
        this.socket.onmessage = (event) => void this.onRelayMessage(event.data);
        this.socket.onerror = () => console.error('Standard RTC signaling Relay failed');
        this.socket.onclose = () => {
            if (this.heartbeatTimer) window.clearInterval(this.heartbeatTimer);
        };
    }

    private sendIce(candidate: RTCIceCandidate) {
        this.sendPxMessage({
                deviceId: this.clientId,
                streamId: this.grConnParams.clientNonce,
                type: PxProtoMsg.MessageType.values.kSigIceMessage,
                sigIce: {
                    deviceId: this.clientId,
                    ice: candidate.candidate,
                    mid: candidate.sdpMid || '',
                    sdpMlineIndex: candidate.sdpMLineIndex || 0,
                },
            });
    }

    async stop() {
        if (this.heartbeatTimer) window.clearInterval(this.heartbeatTimer);
        this.mediaChannel?.close();
        this.ftChannel?.close();
        this.inputChannel?.close();
        this.pc?.close();
        this.socket?.close();
    }

    private relayType(name: string) {
        return PxProtoMsg.RelayMessageType.values[name];
    }

    private sendRelay(value: Record<string, unknown>) {
        if (this.socket?.readyState !== WebSocket.OPEN) return;
        const msg = PxProtoMsg.RelayMessage.create(value);
        this.socket.send(PxProtoMsg.RelayMessage.encode(msg).finish());
    }

    private async onRelayMessage(raw: unknown) {
        const bytes = raw instanceof ArrayBuffer
            ? new Uint8Array(raw)
            : raw instanceof Blob
                ? new Uint8Array(await raw.arrayBuffer())
                : undefined;
        if (!bytes) return;
        const msg = PxProtoMsg.RelayMessage.decode(bytes) as any;
        if (msg.type === this.relayType('kRelayCreateRoomResp')) {
            this.roomId = msg.createRoomResp?.roomId || '';
            this.sendRelay({
                type: this.relayType('kRelayRequestControl'),
                requestControl: {
                    deviceId: this.clientId,
                    remoteDeviceId: this.grConnParams.deviceId,
                    roomId: this.roomId,
                    deviceName: 'WebClient',
                    streamId: this.grConnParams.clientNonce,
                    forceGdi: false,
                },
            });
        } else if (msg.type === this.relayType('kRelayRoomPrepared')) {
            await this.createAndSendOffer();
        } else if (msg.type === this.relayType('kRelayTargetMessage') && msg.relay?.payload) {
            await this.onPxSignaling(new Uint8Array(msg.relay.payload));
        } else if (msg.type === this.relayType('kRelayError')) {
            console.error('Standard RTC Relay rejected the session');
        }
    }

    private async createAndSendOffer() {
        if (!this.pc) return;
        const offer = await this.pc.createOffer({ offerToReceiveAudio: true, offerToReceiveVideo: true });
        await this.pc.setLocalDescription(offer);
        this.sendPxMessage({
            deviceId: this.clientId,
            streamId: this.grConnParams.clientNonce,
            type: PxProtoMsg.MessageType.values.kSigOfferSdpMessage,
            sigOfferSdp: {
                deviceId: this.clientId,
                sdp: offer.sdp || '',
                connectionTicket: this.grConnParams.ticket,
                clientNonce: this.grConnParams.clientNonce,
                instanceId: this.grConnParams.instanceId || '',
            },
        });
    }

    private sendPxMessage(value: Record<string, unknown>) {
        if (!this.roomId) return;
        const pxMessage = PxProtoMsg.Message.create(value);
        const payload = PxProtoMsg.Message.encode(pxMessage).finish();
        this.sendRelay({
            type: this.relayType('kRelayTargetMessage'),
            fromDeviceId: this.clientId,
            relay: { relayMsgIndex: this.relayIndex++, roomIds: [this.roomId], payload },
        });
    }

    private async onPxSignaling(bytes: Uint8Array) {
        const msg = PxProtoMsg.Message.decode(bytes) as any;
        if (msg.type === PxProtoMsg.MessageType.values.kSigAnswerSdpMessage) {
            await this.pc?.setRemoteDescription({ type: 'answer', sdp: msg.sigAnswerSdp?.sdp || '' });
            for (const candidate of this.pendingRemoteIce.splice(0)) {
                await this.pc?.addIceCandidate(candidate);
            }
            for (const candidate of this.pendingLocalIce.splice(0)) this.sendIce(candidate);
        } else if (msg.type === PxProtoMsg.MessageType.values.kSigIceMessage) {
            const ice = msg.sigIce;
            if (ice?.ice) {
                const candidate: RTCIceCandidateInit = {
                    candidate: ice.ice,
                    sdpMid: ice.mid || null,
                    sdpMLineIndex: ice.sdpMlineIndex || 0,
                };
                if (this.pc?.remoteDescription) await this.pc.addIceCandidate(candidate);
                else this.pendingRemoteIce.push(candidate);
            }
        } else {
            await this.parseMessage(bytes);
        }
    }

    private async onDataMessage(raw: unknown) {
        if (raw instanceof ArrayBuffer) await this.parseMessage(new Uint8Array(raw));
        else if (raw instanceof Blob) await this.parseMessage(new Uint8Array(await raw.arrayBuffer()));
    }
}
