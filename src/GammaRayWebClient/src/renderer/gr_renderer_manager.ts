import { GrProtoMsg } from '@/messages/gr_proto_messages.ts'
import { GrRenderer } from '@/renderer/gr_renderer.ts'
import { GrCanvas2DRenderer } from '@/renderer/gr_renderer_2d.ts'
import { GrWebGLRenderer } from '@/renderer/gr_renderer_webgl.ts'

export class GrRendererManager {
    // renderer
    renderer: GrRenderer
    // render canvas
    rendererCanvas: HTMLCanvasElement
    remoteVideoElement: HTMLVideoElement

    isVideoDecoderInitialized = false
    videoDecoder: VideoDecoder

    constructor(rendererName: string, canvas: HTMLCanvasElement, remoteVideoElement: HTMLVideoElement) {
        this.rendererCanvas = canvas;
        this.remoteVideoElement = remoteVideoElement;
        if (rendererName == '2d') {
            this.renderer = new GrCanvas2DRenderer(rendererName, canvas)
        } else if (rendererName == 'webgl') {
            this.renderer = new GrWebGLRenderer(rendererName, canvas)
        }
    }

    onVideoFrame(msg: any): void {
        const isH265 = msg.videoFrame.type == GrProtoMsg.VideoType.values.kNetHevc
        const isH264 = msg.videoFrame.type == GrProtoMsg.VideoType.values.kNetH264
        //console.log("video type.", isH265, isH264);
        if (msg.videoFrame.key && !this.isVideoDecoderInitialized) {
            console.log('this is key frame, will init')

            const connInstance = this

            this.videoDecoder = new VideoDecoder({
                output(frame) {
                    //renderer.updateChromeSize(chromeWidth, chromeHeight);
                    console.log('decode frame : ', frame)
                    //renderFrame(frame);
                    connInstance.renderer.render(frame)
                    //frame.close();
                },
                error(e) {
                    connInstance.isVideoDecoderInitialized = false
                    console.log('**************** decode error : ', e)
                },
            });

            if (isH265) {
                const config = {
                    codec: 'hev1.1.6.L150.90',
                    // codedWidth: 1920,
                    // codedHeight: 1080,
                    //hardwareAcceleration: 'no-preference',
                }

                console.log('config h265 : ', config)
                this.videoDecoder.configure(config)
            } else if (isH264) {
                const config = {
                    codec: 'avc1.640034',
                }

                console.log('config h264 : ', config)
                this.videoDecoder.configure(config)
            }

            this.isVideoDecoderInitialized = true
        }

        if (this.videoDecoder && this.isVideoDecoderInitialized) {
            const chunk = new EncodedVideoChunk({
                timestamp: 0,
                type: msg.videoFrame.key ? 'key' : 'delta',
                data: msg.videoFrame.data,
                duration: 0,
            })

            this.videoDecoder.decode(chunk)
        }
    }
}
