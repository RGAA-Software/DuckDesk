import {GrSdk} from "./client/px_sdk.ts";
import {GrRendererManager} from "./renderer/px_renderer_manager.ts";
import {GrConnParams, GrSdkConnType, GrSdkParams} from "./client/px_sdk_params.ts";
import {getBrowserInfo} from "./util/px_browser_info.ts";

export class GrApp {

    // sdk
    grSdk: GrSdk

    // renderer manager
    rendererManager: GrRendererManager

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

        this.rendererManager = new GrRendererManager(rendererName, canvas, remoteVideoElement);

        let sdkConnType = GrSdkConnType.kWebSocket;
        if (connType == "ws") {
            sdkConnType = GrSdkConnType.kWebSocket;
            remoteVideoElement.style.display = "none";
        }
        else if (connType == "rtc_direct") {
            sdkConnType = GrSdkConnType.kWebRtcDirect;
            canvas.style.display = "none";
        }
        else if (connType == "rtc") {
            sdkConnType = GrSdkConnType.kWebRtc;
            canvas.style.display = "none";
        }

        this.grSdk = new GrSdk(new GrSdkParams({
            sdkType: sdkConnType,
            canvas: canvas,
            rendererName: rendererName
        }), this.rendererManager);

        this.grSdk.start(new GrConnParams({
            //host: "10.0.0.16",
            // host: "10.0.0.112",
            host: hostParam,
            port: 20371
        }));

        console.log("browse info: ", getBrowserInfo());

    }

}
