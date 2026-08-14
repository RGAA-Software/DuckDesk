class Renderer {
    
    constructor(name) {
        this.rendererName = name;
        this.lastRenderTime = performance.now();
        this.fps = 0;
    }

    updateChromeSize(width, height) {
        this.chromeWidth = width;
        this.chromeHeight = height;
    }

    registerFpsCallback(callback) {
        this.fpsCallback = callback;
    }

    render(frame) {
        let currentTime = performance.now();
        let duration = (currentTime - this.lastRenderTime);
        this.fps++;
        if (duration > 1000) {
            this.lastRenderTime = currentTime;
            this.fpsCallback(this.fps);
            this.fps = 0;
        }
    }
}