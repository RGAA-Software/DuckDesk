import {GrRenderer} from "./gr_renderer.ts";

export class GrCanvas2DRenderer extends GrRenderer {
    constructor(name: string, renderCanvas: HTMLCanvasElement) {
        super(name, renderCanvas)
        this.renderCanvasContext = renderCanvas.getContext('2d')
    }
    
    render(frame: any) {
        super.render(frame)
        //const factor = this.chromeWidth * 1.0 / frame.displayWidth;
        console.log("frame:", frame.width, frame.height)
        const factor = 0.8
        this.renderCanvas.width = frame.displayWidth * factor
        this.renderCanvas.height = frame.displayHeight * factor
        this.renderCanvasContext.drawImage(
            frame,
            0,
            0,
            frame.displayWidth * factor,
            frame.displayHeight * factor,
        )
        frame.close()
    }
}
