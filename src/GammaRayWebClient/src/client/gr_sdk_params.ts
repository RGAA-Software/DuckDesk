// connection type
export enum GrSdkConnType {
    kWebSocket,
    kWebRtcDirect,
}

// sdk params
export class GrSdkParams {
    // type
    sdkType: GrSdkConnType;
    
    // canvas
    canvas: HTMLCanvasElement;
    
    // renderer name
    // 2d / webgl / webgpu
    rendererName: string
    
    constructor(params: { sdkType: GrSdkConnType; canvas: HTMLCanvasElement; rendererName: string }) {
        this.sdkType = params.sdkType;
        this.canvas = params.canvas;
        this.rendererName = params.rendererName;
    }
}

// conn params
export class GrConnParams {
    host: string;
    port: number;
    constructor(params: { host: string; port: number }) {
        this.host = params.host;
        this.port = params.port;
    }
}
