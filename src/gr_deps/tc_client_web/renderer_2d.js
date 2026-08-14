class Canvas2DRenderer extends Renderer {
	#canvas = null;
	#ctx = null;

	constructor(type, canvas) {
		super(type);
		this.#canvas = canvas;
		this.#ctx = canvas.getContext(type);
	}

	render(frame) {
		super.render(frame);
		let factor = this.chromeWidth * 1.0 / frame.displayWidth;
		this.#canvas.width = frame.displayWidth * factor;
		this.#canvas.height = frame.displayHeight * factor;
		this.#ctx.drawImage(frame, 0, 0, frame.displayWidth * factor, frame.displayHeight * factor);
		frame.close();
	}
};
