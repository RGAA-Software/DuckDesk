import { GrRenderer } from '@/renderer/gr_renderer.ts'

export class GrWebGLRenderer extends GrRenderer {
    static vertexShaderSource = `
        attribute vec2 inXY;
		attribute vec2 inUV;

        varying highp vec2 uv;

        void main(void) {
            gl_Position = vec4(inXY.x, inXY.y * -1.0, 0.0, 1.0);
            uv = inUV;
        }
    `

    static fragmentShaderSource = `
        varying highp vec2 uv;

        uniform sampler2D texture;

        void main(void) {
            gl_FragColor = texture2D(texture, uv);
        }
    `

    constructor(name: string, renderCanvas: HTMLCanvasElement) {
        super(name, renderCanvas)
        
        const gl = (this.renderCanvasContext = renderCanvas.getContext(name, {antialias: true}));

        const vertexShader = gl.createShader(gl.VERTEX_SHADER)
        gl.shaderSource(vertexShader, GrWebGLRenderer.vertexShaderSource)
        gl.compileShader(vertexShader)
        if (!gl.getShaderParameter(vertexShader, gl.COMPILE_STATUS)) {
            throw gl.getShaderInfoLog(vertexShader)
        }

        const fragmentShader = gl.createShader(gl.FRAGMENT_SHADER)
        gl.shaderSource(fragmentShader, GrWebGLRenderer.fragmentShaderSource)
        gl.compileShader(fragmentShader)
        if (!gl.getShaderParameter(fragmentShader, gl.COMPILE_STATUS)) {
            throw gl.getShaderInfoLog(fragmentShader)
        }

        const shaderProgram = gl.createProgram()
        gl.attachShader(shaderProgram, vertexShader)
        gl.attachShader(shaderProgram, fragmentShader)
        gl.linkProgram(shaderProgram)
        if (!gl.getProgramParameter(shaderProgram, gl.LINK_STATUS)) {
            throw gl.getProgramInfoLog(shaderProgram)
        }
        gl.useProgram(shaderProgram)

        // Vertex coordinates, clockwise from bottom-left.
        const vertexBuffer = gl.createBuffer()
        gl.bindBuffer(gl.ARRAY_BUFFER, vertexBuffer)
        gl.bufferData(
            gl.ARRAY_BUFFER,
            new Float32Array([-1.0, -1.0, -1.0, +1.0, +1.0, +1.0, +1.0, -1.0]),
            gl.STATIC_DRAW,
        )

        const xyLocation = gl.getAttribLocation(shaderProgram, 'inXY')
        gl.vertexAttribPointer(xyLocation, 2, gl.FLOAT, false, 0, 0)
        gl.enableVertexAttribArray(xyLocation)

        //
        const uvBuffer = gl.createBuffer()
        gl.bindBuffer(gl.ARRAY_BUFFER, uvBuffer)
        gl.bufferData(
            gl.ARRAY_BUFFER,
            new Float32Array([0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0, 0.0]),
            gl.STATIC_DRAW,
        )

        const uvLocation = gl.getAttribLocation(shaderProgram, 'inUV')
        gl.vertexAttribPointer(uvLocation, 2, gl.FLOAT, false, 0, 0)
        gl.enableVertexAttribArray(uvLocation)

        const texture = gl.createTexture()
        gl.bindTexture(gl.TEXTURE_2D, texture)
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE)
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE)
    }

    render(frame: VideoFrame) {
        super.render(frame)
        // const factor = 0.8;//(this.chromeWidth * 1.0) / frame.displayWidth
        //
        // this.renderCanvas.width = frame.displayWidth * factor
        // this.renderCanvas.height = frame.displayHeight * factor
        
        const viewportWidth = window.innerWidth;
        const viewportHeight = window.innerHeight;
        
        if (this.displayMode === 'fit-width') {
            this.renderCanvas.width = viewportWidth;
            this.renderCanvas.height = frame.displayHeight * (viewportWidth / frame.displayWidth);
        } else if (this.displayMode === 'fit-height') {
            this.renderCanvas.height = viewportHeight;
            this.renderCanvas.width = frame.displayWidth * (viewportHeight / frame.displayHeight);
            
        }
        
        const gl = this.renderCanvasContext

        gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA, gl.RGBA, gl.UNSIGNED_BYTE, frame)
        frame.close()

        //gl.viewport(0, 0, gl.drawingBufferWidth, gl.drawingBufferHeight)
        gl.viewport(0, 0, this.renderCanvas.width, this.renderCanvas.height)
        gl.clearColor(1.0, 0.0, 0.0, 1.0)
        gl.clear(gl.COLOR_BUFFER_BIT)

        gl.drawArrays(gl.TRIANGLE_FAN, 0, 4)
    }
}
