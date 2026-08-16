// connection type
export enum PxSdkConnType {
    kWebSocket = 0,
    kWebRtcDirect = 1,
    kWebRtc = 2,
}

// sdk params
export class PxSdkParams {
    // type
    sdkType: PxSdkConnType;

    // canvas
    canvas: HTMLCanvasElement;

    // renderer name
    // 2d / webgl / webgpu
    rendererName: string

    constructor(params: { sdkType: PxSdkConnType; canvas: HTMLCanvasElement; rendererName: string }) {
        this.sdkType = params.sdkType;
        this.canvas = params.canvas;
        this.rendererName = params.rendererName;
    }
}

// conn params
export class PxConnParams {
    host: string;
    port: number;
    constructor(params: { host: string; port: number }) {
        this.host = params.host;
        this.port = params.port;
    }
}
