import {GrConnParams, GrSdkConnType, GrSdkParams} from "./px_sdk_params.ts";
import {GrConn} from "./px_conn.ts";
import {GrRendererManager} from "../renderer/px_renderer_manager.ts";
import {GrWsConn} from "./px_ws_conn.ts";
import {GrRtcDirectConn} from "./px_rtc_direct_conn.ts";

export class GrSdk {

    // params
    sdkParams: GrSdkParams;

    // stream conn
    streamConn: GrConn

    // renderer manager
    rendererManager: GrRendererManager

    // browser info
    browserInfo: Record<string, any>

    constructor(params: GrSdkParams, rendererManager: GrRendererManager) {
        this.sdkParams = params;
        this.rendererManager = rendererManager;
        //this.browserInfo = getBrowserInfo();
    }

    start(connParams: GrConnParams): void {
        console.log("connParams:", connParams);
        if (this.sdkParams.sdkType == GrSdkConnType.kWebSocket) {
            this.startWithWss(connParams);
        }
        else if (this.sdkParams.sdkType == GrSdkConnType.kWebRtcDirect) {
            this.startWithRtcDirect(connParams);
        }
        else {
            console.log("unknown sdk type: ", this.sdkParams.sdkType);
        }
    }

    private startWithWss(connParams: GrConnParams) {
        console.log("startWithWs")
        this.streamConn = new GrWsConn(this, connParams, this.rendererManager);
        this.streamConn.start();
    }

    private startWithRtcDirect(connParams: GrConnParams) {
        console.log("startWithRtcDirect")
        this.streamConn = new GrRtcDirectConn(this, connParams, this.rendererManager);
        this.streamConn.start();
    }


}
