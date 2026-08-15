import {GrSdk} from "../client/px_sdk.ts";
import {GrRendererManager} from "../renderer/px_renderer_manager.ts";
import {GrProtoMsg} from "./px_proto_messages.ts";

export class GrProtoProcessor {
    // sdk
    grSdk: GrSdk
    
    // renderer manager
    rendererManager: GrRendererManager
    
    constructor(sdk: GrSdk, rendererManager: GrRendererManager) {
        this.grSdk = sdk;
        this.rendererManager = rendererManager;
    }
    
    async parseMessage(data: Uint8Array) {
        const msg = GrProtoMsg.Message.decode(data);
        const msgType = msg.type;
        
        if (msgType == GrProtoMsg.MessageType.values.kVideoFrame) {
            await this.rendererManager.onVideoFrame(msg);
        }
    }
}