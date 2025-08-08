import { GrConn } from '@/client/gr_conn.ts'
import { GrSdk } from '@/client/gr_sdk.ts'
import { GrConnParams } from '@/client/gr_sdk_params.ts'
import { GrRendererManager } from '@/renderer/gr_renderer_manager.ts'

export class GrRtcDirectConn extends GrConn {
    
    constructor(sdk: GrSdk, params: GrConnParams, rendererManager: GrRendererManager) {
        super(sdk, params, rendererManager)
    }
    
    start() {
        super.start()
    }
    
    stop() {
        super.stop()
    }
    
}
