import { GrSdk } from '@/client/gr_sdk.ts'
import { GrProtoMsg } from '@/messages/gr_proto_messages.ts'
import { GrRendererManager } from '@/renderer/gr_renderer_manager.ts'

export class GrProtoProcessor {
    // sdk
    grSdk: GrSdk
    
    // renderer manager
    rendererManager: GrRendererManager
    
    constructor(sdk: GrSdk, rendererManager: GrRendererManager) {
        this.grSdk = sdk;
        this.rendererManager = rendererManager;
    }
    
    parseMessage(data: Uint8Array) {
        const msg = GrProtoMsg.Message.decode(data);
        const msgType = msg.type;
        
        if (msgType == GrProtoMsg.MessageType.values.kVideoFrame) {
            this.rendererManager.onVideoFrame(msg);
        }
    }
}