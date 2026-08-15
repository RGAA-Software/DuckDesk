'use strict';

importScripts("renderer.js", "renderer_2d.js", "renderer_webgl.js", "renderer_webgpu.js", "protobuf.js");

let renderer = null;
let pendingFrame = null;
let startTime = null;
let frameCount = 0;

function renderFrame(frame) {
    // if (!pendingFrame) {
    //     // Schedule rendering in the next animation frame.
    //     requestAnimationFrame(renderAnimationFrame);
    // } else {
    //     // Close the current pending frame before replacing it.
    //     pendingFrame.close();
    // }
    // // Set or replace the pending frame.
    // pendingFrame = frame;

    renderer.render(frame);
    frame.close();
}

function renderAnimationFrame() {
    renderer.render(pendingFrame);
    pendingFrame = null;
}

let isDecoderInited = false;
let isSendInfomation = false;
let chromeWidth = 0;
let chromeHeight = 0;
let baseAddress = "";
let lastReceivedTime = performance.now();
let receivedFps = 0;

function isKeyFrame(frame, isH265) {
    if (isH265) {
        for (let i = 2; i + 1 < frame.length; i++) {
            if (0x01 === frame[i] && 0x00 === frame[i-1] && 0x00 === frame[i-2]) {
                let type = (frame[i+1] >> 1) & 0x3f;
                if (type < 32) {
                    return (16 <= type && type <= 23) ? 1 : 0;
                }
            }
        }
    }
    else {
        if (frame[0] === 0x00 && frame[1] === 0x00 && frame[2] === 0x00 && frame[3] === 0x01) {
            let type = frame[4] & 0x1f;
            if (type === 0x07 || type === 0x08 || type === 0x05) {
                return true;
            }
        }
        if (frame[0] === 0x00 && frame[1] === 0x00 && frame[2] === 0x01) {
            let type = frame[4] & 0x1f;
            if (type === 0x07 || type === 0x08 || type === 0x05) {
                return true;
            }
        }
    }
    return false;
}

function start({ prefix, rendererName, canvas }) {

    switch (rendererName) {
        case "2d":
            renderer = new Canvas2DRenderer(rendererName, canvas);
            break;
        case "webgl":
            renderer = new WebGLRenderer(rendererName, canvas);
            break;
        case "webgpu":
            renderer = new WebGPURenderer(canvas);
            break;
    }

    renderer.registerFpsCallback(function(fps) {
        postMessage({"prefix":"fps", "fps": fps});
    });
    
    const videoDecoder = new VideoDecoder({
        output(frame) {
            renderer.updateChromeSize(chromeWidth, chromeHeight);
            //console.log("decode frame : ", frame);
            renderFrame(frame);
        },
        error(e) {
            isDecoderInited = false;
            setStatus("decode", e);
            console.log("decode error : ", e);
        }
    });

    protobuf.load(["tc_message.proto"], function (err, root) {
        if (err != null) {
            console.log("load protobuf failed : ", err);
            return;
        }

        console.log("root : ", root);

        let streamSocket = new WebSocket("ws://" + baseAddress);
        streamSocket.binaryType = 'arraybuffer';
        streamSocket.onopen = (e) => {
            console.log("stream opened ...");
        };

        streamSocket.onmessage = (event) => {
            let data = new Uint8Array(event.data);
            let decodedData = root.nested.tc.Message.decode(data);
            //console.log("frame : ", decodedData);

            if (decodedData.type === root.nested.tc.MessageType.kVideoFrame) {
            
                let isH265 = decodedData.videoFrame.type === root.nested.tc.VideoType.kNetH265;
                let isH264 = decodedData.videoFrame.type === root.nested.tc.VideoType.kNetH264;
                let isVp9 = decodedData.videoFrame.type === root.nested.tc.VideoType.kNetVp9;

                console.log("type: ", decodedData.videoFrame.type, isH265, isH264, isVp9);

                if (!isDecoderInited) {
                    //codec: "hev1.1.6.L150.90",
                    //codec: "hev1.1.6.L123.90"
                    //codec: "avc1.640034",
                    if (isH265) {
                        const config = {
                            codec: "hev1.1.6.L150.90",
                            // codedWidth: 1920,
                            // codedHeight: 1080,
                            //hardwareAcceleration: 'no-preference',
                        };

                        console.log("config h265 : ", config);
                        videoDecoder.configure(config);
                    }
                    else if (isH264) {
                        const config = {
                            codec: "avc1.640034",
                        };

                        console.log("config h264 : ", config);
                        videoDecoder.configure(config);
                    }
                    else if (isVp9) {
                        const config = {
                            codec: "vp09.00.10.08",
                            width: 1920,
                            height: 1080,
                            bitrate: 8_000_000, // 8 Mbps
                            framerate: 30,
                        };

                        console.log("config vp9 : ", config);
                        videoDecoder.configure(config);
                    }

                    isDecoderInited = true;
                }

                receivedFps++;
                let currentTime = performance.now();
                let duration = currentTime - lastReceivedTime;
                if (duration > 1000) {
                    console.log("received fps : ", receivedFps);
                    lastReceivedTime = currentTime;
                    receivedFps = 0;
                }

                if (isDecoderInited) {

                    let keyFrame = isKeyFrame(decodedData.videoFrame.data, isH265);
                    
                    let chunk = new EncodedVideoChunk({
                        timestamp: 0,
                        type: keyFrame ? "key" : "delta",
                        data: decodedData.videoFrame.data,
                        duration: 0
                    });

                    // infomation
                    if (!isSendInfomation) {
                        let frameType = "";
                        if (isH265) {
                            frameType = "H265";
                        } else if (isH264) {
                            frameType = "H264";
                        } else if (isVp9) {
                            frameType = "VP9";
                        }
                        let infomation = "Format : " + frameType;// + " Size : " + decodedData.frame.width + "x" + decodedData.frame.height;
                        postMessage({"prefix": "information", "information": infomation});

                        isSendInfomation = true;
                    }

                    videoDecoder.decode(chunk);
                }
            }
        };

        streamSocket.onclose = function (event) {
            if (event.wasClean) {
                alert(`[close] Connection closed cleanly, code=${event.code} reason=${event.reason}`);
            } else {

            }
        };

        streamSocket.onerror = function (error) {
            alert(`[error] ${error.message}`);
        };

    });

}

self.addEventListener("message", message => {
    console.log("message.data : ", message.data);
    let prefix = message.data.prefix;
    if (prefix === "start") {
        start(message.data);
    }
    else if (prefix === "chromeSize") {
        chromeWidth = message.data.width;
        chromeHeight = message.data.height;
        console.log("chrome w , h ", chromeWidth, chromeHeight);
    }
    else if (prefix == "address") {
        baseAddress = message.data.address;
        console.log("address : ", baseAddress);
    }

}, { once: false });
