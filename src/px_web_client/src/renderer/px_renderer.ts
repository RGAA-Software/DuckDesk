export class PxRenderer {
    rendererName: string
    lastRenderTime: number
    fps: number
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
    
    render(frame: any) {
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
