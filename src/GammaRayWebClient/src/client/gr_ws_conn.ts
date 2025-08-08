import { GrConn } from '@/client/gr_conn.ts'
import { GrSdk } from '@/client/gr_sdk.ts'
import { GrConnParams } from '@/client/gr_sdk_params.ts'
import { GrRendererManager } from '@/renderer/gr_renderer_manager.ts'

export class GrWsConn extends GrConn {
    
    // ws
    websocket: WebSocket
    
    connected: boolean = false;
    hbIndex: number = 0;
    timerId: number = 0;
    
    constructor(sdk: GrSdk, params: GrConnParams, rendererManager: GrRendererManager) {
        super(sdk, params, rendererManager)
    }
    
    start() {
        const url = "ws://" + this.grConnParams.host + ":" + this.grConnParams.port + '/media?only_audio=0&remote_device_id=2222&stream_id=1122&visitor_device_id=1122';
        //const url = "ws://10.0.0.16:20371/media?only_audio=0&remote_device_id=2222&stream_id=1122&visitor_device_id=1122";
        console.log(url);
        this.websocket = new WebSocket(url);
        
        this.websocket.onopen = (e) => {
            this.connected = true;
            // let msg = SignalingMaker.makeHelloMessage(this.sigParams.clientId, this.sigParams.sigToken);
            // this.websocket?.send(msg);
            console.log("websocket open, event: ", e);
        }
        
        this.websocket.onclose = (e) => {
            console.log("websocket close", e);
            this.connected = false;
        }
        
        this.websocket.onerror = (e) => {
            console.log("websocket error: ", e);
            this.connected = false;
        }
        
        this.websocket.onmessage = (e) => {
            //console.log("onmessage:", e.data)
            const data = e.data;
            
            if (data instanceof ArrayBuffer) {
                const uint8Array = new Uint8Array(data);
                this.parseMessage(uint8Array);
            }
            else if (data instanceof Blob) {
                const reader = new FileReader();
                reader.onload = () => {
                    const arrayBuffer = reader.result as ArrayBuffer;
                    const finalUint8Array = new Uint8Array(arrayBuffer);
                    this.parseMessage(finalUint8Array);
                };
                reader.readAsArrayBuffer(data);
                return;
            }
            else {
                console.error('未知的数据类型:', typeof data);
                return;
            }
        }
        
        this.timerId = setInterval(() => {
            if (!this.connected || this.websocket == undefined) {
                return;
            }
            
            //this.websocket?.send(msg);
        }, 1000);
        
    }
    
    stop() {
        super.stop();
    }
    
    parseMessage(data: Uint8Array) {
        super.parseMessage(data);
    }
}
