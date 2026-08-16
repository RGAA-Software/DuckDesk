import {PxSdk} from "./px_sdk.ts";
import {PxConnParams, PxSdkParams} from "./px_sdk_params.ts";
import {PxProtoProcessor} from "../messages/px_proto_processor.ts";
import {PxRendererManager} from "../renderer/px_renderer_manager.ts";

export class PxConn {
    // sdk
    grSdk: PxSdk

    // sdk params
    grSdkParams: PxSdkParams

    // conn params
    grConnParams: PxConnParams

    // messages processor
    protoProcessor: PxProtoProcessor

    // renderer manager
    grRendererManager: PxRendererManager

    constructor(sdk: PxSdk, connParams: PxConnParams, rendererManager: PxRendererManager) {
        this.grSdk = sdk
        this.grSdkParams = sdk.sdkParams
        this.grConnParams = connParams
        this.grRendererManager = rendererManager
        this.protoProcessor = new PxProtoProcessor(sdk, rendererManager);
    }

    async start() {

    }

    async stop() {

    }

    async parseMessage(data: Uint8Array) {
        await this.protoProcessor.parseMessage(data);
    }

}
