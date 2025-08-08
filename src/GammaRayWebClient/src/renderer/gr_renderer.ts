export class GrRenderer {
    rendererName: string
    lastRenderTime: number
    fps: number
    frame: VideoFrame
    renderCanvas: HTMLCanvasElement
    renderCanvasContext = null;
    displayMode: string
    
    constructor(name: string, renderCanvas: HTMLCanvasElement) {
        this.rendererName = name;
        this.lastRenderTime = performance.now();
        this.fps = 0;
        this.renderCanvas = renderCanvas;
        this.displayMode = "fit-height";
    }
    
    render(frame: VideoFrame) {
        this.frame = frame
        const currentTime = performance.now()
        const duration = currentTime - this.lastRenderTime
        this.fps++
        if (duration > 1000) {
            this.lastRenderTime = currentTime
            //this.fpsCallback(this.fps)
            this.fps = 0
        }
    }
}
