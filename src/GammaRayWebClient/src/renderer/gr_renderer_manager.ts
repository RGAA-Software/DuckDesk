import {GrRenderer} from "./gr_renderer.ts";
import {GrCanvas2DRenderer} from "./gr_renderer_2d.ts";
import {GrWebGLRenderer} from "./gr_renderer_webgl.ts";
import {GrProtoMsg} from "../messages/gr_proto_messages.ts";
import '@libmedia/cheap/cheapdef';
import * as demux from '@libmedia/avformat/demux';
import { createAVIFormatContext } from '@libmedia/avformat/AVFormatContext';
import {
    createAVPacket,
    destroyAVPacket,
} from '@libmedia/avutil/util/avpacket';
import { AVMediaType } from '@libmedia/avutil/codec';
import compileResource from '@libmedia/avutil/function/compileResource';
import WasmVideoDecoder from '@libmedia/avcodec/wasmcodec/VideoDecoder';
import Sleep from '@libmedia/common/timer/Sleep';
import { destroyAVFrame } from '@libmedia/avutil/util/avframe';
import { AVCodecID, AVMediaType } from '@libmedia/avutil/codec'
import AVCodecParameters from "@libmedia/avutil/struct/avcodecparameters";
import { AVChromaLocation, AVColorPrimaries, AVColorRange, AVColorSpace, AVColorTransferCharacteristic, AVFieldOrder, AVPixelFormat } from "@libmedia/avutil/pixfmt";

import AVPacket from "@libmedia/avutil/struct/avpacket";
import AVPacketFlags from "@libmedia/avutil/struct/avpacket";
import AVFrame, { AVFramePool } from "@libmedia/avutil/struct/avframe";
import { WebAssemblyResource } from "@libmedia/cheap/webassembly/compiler";
import { Data } from "@libmedia/common/types/type";

import isWorker from "@libmedia/common/function/isWorker";
import { mapUint8Array, memcpyFromUint8Array } from '@libmedia/cheap/std/memory'
import { avFree, avMalloc } from '@libmedia/avutil/util/mem'

import {
    formatUrl,
    getIOReader,
    getAVFormat,
    getAccept,
    getWasm,
} from '../utils';

import WebGPURender, { WebGPURenderOptions } from '@libmedia/avrender/image/WebGPURender'
import WebGLRender , { ImageRenderOptions } from '@libmedia/avrender/image/WebGLRender'
import { RenderMode } from '@libmedia/avrender/image/ImageRender'
import CanvasImageRender from 'a@libmedia/vrender/image/Canvas2dRender'
import WebGPUExternalRender from '@libmedia/avrender/image/WebGPUExternalRender'
import WebGLDefault8Render from '@libmedia/avrender/image/WebGLDefault8Render'
import WebGLDefault16Render from '@libmedia/avrender/image/WebGLDefault16Render'
import WebGPUDefault8Render from '@libmedia/avrender/image/WebGPUDefault8Render'
import WebGPUDefault16Render from '@libmedia/avrender/image/WebGPUDefault16Render'
import WritableStreamRender from '@libmedia/avrender/image/WritableStreamRender'

export class GrRendererManager {
    // renderer
    // renderer: GrRenderer
    renderer: WebGLRender
    // render canvas
    rendererCanvas: any
    remoteVideoElement: HTMLVideoElement

    isVideoDecoderInitialized = false
    // videoDecoder: VideoDecoder
    videoDecoder: WasmVideoDecoder
    fpsCount: number = 0;

    frameWidth: number = 0;
    frameHeight: number = 0;

    canvasWidth: number = 0;
    canvasHeight: number = 0;

    constructor(rendererName: string, canvas: any, remoteVideoElement: HTMLVideoElement) {
        this.rendererCanvas = canvas;
        this.remoteVideoElement = remoteVideoElement;
        if (rendererName == '2d') {
            //this.renderer = new GrCanvas2DRenderer(rendererName, canvas)
        } else if (rendererName == 'webgl') {
            //this.renderer = new GrWebGLRenderer(rendererName, canvas)
        }

        console.log("isWorkder: ", isWorker());

        const timerId = setInterval(() => {
            //console.log("fps: ", this.fpsCount);
            this.fpsCount = 0;
        }, 1000);

    }

    async onVideoFrame(msg: any) {
        const isH265 = msg.videoFrame.type == GrProtoMsg.VideoType.values.kNetHevc
        const isH264 = msg.videoFrame.type == GrProtoMsg.VideoType.values.kNetH264
        //console.log("video type.", isH265, isH264);
        if (msg.videoFrame.key && !this.isVideoDecoderInitialized) {
            console.log('this is key frame, will init, ratio: ', window.devicePixelRatio)

            this.renderer = new WebGLDefault8Render(this.rendererCanvas, {
                renderMode: RenderMode.FIT,
                devicePixelRatio: window.devicePixelRatio
            });

            await this.renderer.init();

            const connInstance = this
            const codedId = AVCodecID.AV_CODEC_ID_H264;/**/
            //const codedId = AVCodecID.AV_CODEC_ID_HEVC;

            const res = await compileResource(
                getWasm('decoder', codedId),
                true
            );
            console.log("wasm res: ", res);
            this.videoDecoder = new WasmVideoDecoder({
                resource: res,
                onReceiveAVFrame: (frame) => {
                    connInstance.frameWidth = msg.videoFrame.frameWidth;
                    connInstance.frameHeight = msg.videoFrame.frameHeight;
                    
                    const apsectRatio = connInstance.frameWidth * 1.0 / connInstance.frameHeight;

                    const targetHeight = window.innerHeight;
                    const targetWidth = Math.trunc(targetHeight * apsectRatio);
                    
                    console.log("target size:",targetWidth, targetHeight);
                    
                    if (connInstance.canvasWidth != targetWidth || connInstance.canvasHeight != targetHeight) {
                        this.renderer.viewport(targetWidth, targetHeight);
                        connInstance.canvasWidth = targetWidth;
                        connInstance.canvasHeight = targetHeight;
                        console.log("set viewport to: ", targetWidth, targetHeight);
                    }

                    connInstance.fpsCount += 1;
                    connInstance.renderer.render(frame);

                    // connInstance.rendererCanvas.width = 1280;
                    // connInstance.rendererCanvas.height = 720;
                    // console.log("width: ", window.innerWidth, ", height: ", window.innerHeight);
                    // console.log(
                    //     `got video frame, pts: ${frame.pts}, duration: ${frame.duration}, frame: `, frame, '\n');
                    destroyAVFrame(frame);
                },
            });
            console.log('decoder, url:', getWasm('decoder', codedId));
            console.log("decoder: ", this.videoDecoder);

            const params = make<AVCodecParameters>(new AVCodecParameters())
            // const params = new AVCodecParameters();
            params.codecType = AVMediaType.AVMEDIA_TYPE_VIDEO;
            params.codecId = codedId;
            // params.width = 1280;
            // params.height = 720;
            params.format = AVPixelFormat.AV_PIX_FMT_YUV420P;
            console.log("codec params: ", params);

            const ret = await this.videoDecoder.open(addressof(params));
            if (ret) {
                console.log(`open decode error: ${ret}\n`);
                return;
            }
            console.log("codec for : ", params.codecId, " load success");
            this.isVideoDecoderInitialized = true;
            // this.videoDecoder = new VideoDecoder({
            //     output(frame) {
            //         //renderer.updateChromeSize(chromeWidth, chromeHeight);
            //         console.log('decode frame : ', frame)
            //         //renderFrame(frame);
            //         connInstance.renderer.render(frame)
            //         //frame.close();
            //     },
            //     error(e) {
            //         connInstance.isVideoDecoderInitialized = false
            //         console.log('**************** decode error : ', e)
            //     },
            // });

            //     if (isH265) {
            //         const config = {
            //             codec: 'hev1.1.6.L150.90',
            //             // codedWidth: 1920,
            //             // codedHeight: 1080,
            //             //hardwareAcceleration: 'no-preference',
            //         }
            //
            //         console.log('config h265 : ', config)
            //         this.videoDecoder.configure(config)
            //     } else if (isH264) {
            //         const config = {
            //             codec: 'avc1.640034',
            //         }
            //
            //         console.log('config h264 : ', config)
            //         this.videoDecoder.configure(config)
            //     }
            //
            //     this.isVideoDecoderInitialized = true
            // }
            //
            // if (this.videoDecoder && this.isVideoDecoderInitialized) {
            //     const chunk = new EncodedVideoChunk({
            //         timestamp: 0,
            //         type: msg.videoFrame.key ? 'key' : 'delta',
            //         data: msg.videoFrame.data,
            //         duration: 0,
            //     })
            //
            //     this.videoDecoder.decode(chunk)
        }

        if (this.isVideoDecoderInitialized) {

            const packet = make<AVPacket>(new AVPacket());
            const timestamp = Date.parse(new Date().toString());
            packet.pts = BigInt(timestamp);
            //packet.bitFormat = AVPixelFormat.AV_PIX_FMT_YUV420P;

            const data = msg.videoFrame.data;
            const avData = avMalloc(data.length);
            memcpyFromUint8Array(avData, data.length, data);
            packet.data = avData;
            packet.size = data.length;

            if (msg.videoFrame.key) {
                packet.flags = AVPacketFlags.AV_PKT_FLAG_KEY;
            }
            const ret = this.videoDecoder.decode(addressof(packet));
            // console.log("decode ret: ", ret, packet.pts, ", datasize: ", data.length);
            avFree(avData);
            if (ret != 0) {
            }

        }

    }
}
