import {PxSdk} from "../client/px_sdk.ts";
import {PxRendererManager} from "../renderer/px_renderer_manager.ts";
import {PxProtoMsg} from "./px_proto_messages.ts";

export class PxProtoProcessor {
    // sdk
    grSdk: PxSdk
    
    // renderer manager
    rendererManager: PxRendererManager
    
    constructor(sdk: PxSdk, rendererManager: PxRendererManager) {
        this.grSdk = sdk;
        this.rendererManager = rendererManager;
    }
    
    async parseMessage(data: Uint8Array) {
        const msg = PxProtoMsg.Message.decode(data);
        const msgType = msg.type;
        
        if (msgType == PxProtoMsg.MessageType.values.kVideoFrame) {
            await this.rendererManager.onVideoFrame(msg);
        }
    }
}