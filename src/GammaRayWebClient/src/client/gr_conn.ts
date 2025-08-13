import {GrSdk} from "./gr_sdk.ts";
import {GrConnParams, GrSdkParams} from "./gr_sdk_params.ts";
import {GrProtoProcessor} from "../messages/gr_proto_processor.ts";
import {GrRendererManager} from "../renderer/gr_renderer_manager.ts";

export class GrConn {
    // sdk
    grSdk: GrSdk

    // sdk params
    grSdkParams: GrSdkParams

    // conn params
    grConnParams: GrConnParams

    // messages processor
    protoProcessor: GrProtoProcessor

    // renderer manager
    grRendererManager: GrRendererManager

    constructor(sdk: GrSdk, connParams: GrConnParams, rendererManager: GrRendererManager) {
        this.grSdk = sdk
        this.grSdkParams = sdk.sdkParams
        this.grConnParams = connParams
        this.grRendererManager = rendererManager
        this.protoProcessor = new GrProtoProcessor(sdk, rendererManager);
    }

    async start() {

    }

    async stop() {

    }

    async parseMessage(data: Uint8Array) {
        await this.protoProcessor.parseMessage(data);
    }

}
