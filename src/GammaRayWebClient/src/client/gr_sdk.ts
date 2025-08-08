import { GrConnParams, GrSdkConnType, type GrSdkParams } from '@/client/gr_sdk_params.ts'
import { GrConn } from '@/client/gr_conn.ts'
import { GrWsConn } from '@/client/gr_ws_conn.ts'
import { GrRtcDirectConn } from '@/client/gr_rtc_direct_conn.ts'
import { GrRendererManager } from '@/renderer/gr_renderer_manager.ts'
import { getBrowserInfo } from '@/util/gr_browser_info.ts'

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
        this.browserInfo = getBrowserInfo();
    }
    
    start(connParams: GrConnParams): void {
        console.log("connParams:", connParams);
        if (this.sdkParams.sdkType == GrSdkConnType.kWebSocket) {
            this.startWithWss(connParams);
        }
        else if (this.sdkParams.sdkType == GrSdkConnType.kWebRtcDirect) {
            this.startWithRtcDirect();
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
