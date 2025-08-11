import { GrSdk } from '@/client/gr_sdk.ts'
import { GrConnParams, GrSdkConnType, GrSdkParams } from '@/client/gr_sdk_params.ts'
import { GrRendererManager } from '@/renderer/gr_renderer_manager.ts'

export class GrApp {

    // sdk
    grSdk: GrSdk

    // renderer manager
    rendererManager: GrRendererManager

    grCanvasResizer: CanvasResizer;

    constructor() {
        document.documentElement.style.margin = "0";
        document.documentElement.style.padding = "0";
        document.documentElement.style.overflow = "hidden";
        document.body.style.margin = "0";
        document.body.style.padding = "0";
        document.body.style.overflow = "hidden";
    }

    start(): void {

        const queryParams = new URLSearchParams(window.location.search);
        const aValue = queryParams.get('a');
        console.log('a参数值:', aValue);

        //.transferControlToOffscreen();
        const canvas = document.getElementById("main_view") as HTMLCanvasElement;//.transferControlToOffscreen();
        const remoteVideoElement = document.getElementById('remoteVideo') as HTMLVideoElement;
        const rendererName = "webgl";//"webgl";//2d

        this.rendererManager = new GrRendererManager(rendererName, canvas, remoteVideoElement);

        this.grSdk = new GrSdk(new GrSdkParams({
            sdkType: GrSdkConnType.kWebSocket,
            canvas: canvas,
            rendererName: rendererName
        }), this.rendererManager);

        this.grSdk.start(new GrConnParams({
            host: "10.0.0.16",
            port: 20371
        }));

    }

}
