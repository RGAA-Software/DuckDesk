import {PxConnParams, PxSdkConnType, PxSdkParams} from "./px_sdk_params.ts";
import {PxConn} from "./px_conn.ts";
import {PxRendererManager} from "../renderer/px_renderer_manager.ts";
import {PxWsConn} from "./px_ws_conn.ts";
import {PxRtcDirectConn} from "./px_rtc_direct_conn.ts";

export class PxSdk {

    // params
    sdkParams: PxSdkParams;

    // stream conn
    streamConn: PxConn

    // renderer manager
    rendererManager: PxRendererManager

    // browser info
    browserInfo: Record<string, any>

    constructor(params: PxSdkParams, rendererManager: PxRendererManager) {
        this.sdkParams = params;
        this.rendererManager = rendererManager;
        //this.browserInfo = getBrowserInfo();
    }

    start(connParams: PxConnParams): void {
        console.log("starting connection", {
            host: connParams.host,
            port: connParams.port,
            connectionType: connParams.connectionType,
        });
        if (this.sdkParams.sdkType == PxSdkConnType.kWebSocket) {
            this.startWithWss(connParams);
        }
        else if (this.sdkParams.sdkType == PxSdkConnType.kWebRtcDirect) {
            this.startWithRtcDirect(connParams);
        }
        else {
            console.log("unknown sdk type: ", this.sdkParams.sdkType);
        }
    }

    private startWithWss(connParams: PxConnParams) {
        console.log("startWithWs")
        this.streamConn = new PxWsConn(this, connParams, this.rendererManager);
        this.streamConn.start();
    }

    private startWithRtcDirect(connParams: PxConnParams) {
        console.log("startWithRtcDirect")
        this.streamConn = new PxRtcDirectConn(this, connParams, this.rendererManager);
        this.streamConn.start();
    }


}
