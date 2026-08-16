import {PxSdk} from "./client/px_sdk.ts";
import {PxRendererManager} from "./renderer/px_renderer_manager.ts";
import {PxConnParams, PxSdkConnType, PxSdkParams} from "./client/px_sdk_params.ts";
import {getBrowserInfo} from "./util/px_browser_info.ts";

export class PxApp {

    // sdk
    grSdk: PxSdk

    // renderer manager
    rendererManager: PxRendererManager

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
        const hostParam = queryParams.get('host');
        const connType = queryParams.get('connType');
        console.log('a参数值:', hostParam, connType);

        //.transferControlToOffscreen();
        const canvas = (document.getElementById("main-view") as HTMLCanvasElement);//.transferControlToOffscreen();
        const remoteVideoElement = document.getElementById('remoteVideo') as HTMLVideoElement;
        const rendererName = "webgl";//"webgl";//2d

        this.rendererManager = new PxRendererManager(rendererName, canvas, remoteVideoElement);

        let sdkConnType = PxSdkConnType.kWebSocket;
        if (connType == "ws") {
            sdkConnType = PxSdkConnType.kWebSocket;
            remoteVideoElement.style.display = "none";
        }
        else if (connType == "rtc_direct") {
            sdkConnType = PxSdkConnType.kWebRtcDirect;
            canvas.style.display = "none";
        }
        else if (connType == "rtc") {
            sdkConnType = PxSdkConnType.kWebRtc;
            canvas.style.display = "none";
        }

        this.grSdk = new PxSdk(new PxSdkParams({
            sdkType: sdkConnType,
            canvas: canvas,
            rendererName: rendererName
        }), this.rendererManager);

        this.grSdk.start(new PxConnParams({
            //host: "10.0.0.16",
            // host: "10.0.0.112",
            host: hostParam,
            port: 20371
        }));

        console.log("browse info: ", getBrowserInfo());

    }

}
