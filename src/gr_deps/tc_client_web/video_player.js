const startButton = document.querySelector("#start");

let addressInput = document.querySelector("#adress");
let fpsLabel = document.querySelector("#fps");
let infomationLabel = document.querySelector("#infomation");
let worker = null;

startButton.addEventListener("click", () => {
    document.querySelectorAll("input").forEach(input => input.disabled = true);
    startButton.disabled = true;

    start();

}, { once: true });

function processMessage(message) {
    //console.log(`message.data : `, message.data);
    if (message.data.prefix === "fps") {
        fpsLabel.innerText = message.data.fps.toString();
    }
    else if (message.data.prefix === "information") {
        infomationLabel.innerText = message.data.information;
    }
}

window.onresize = function() {
    updateChromeSize();
}

function updateChromeSize() {
    if (!worker) {
        return;
    }
    const prefix = "chromeSize";
    let width = window.innerWidth;
    let height = window.innerHeight;
    worker.postMessage({ prefix, width, height });
}

function start() {
    const canvas = document.querySelector("canvas").transferControlToOffscreen();
    worker = new Worker("./video_player_worker.js");
    worker.addEventListener("message", processMessage);
    
    updateChromeSize();

    {
        const prefix = "address";
        let address = addressInput.value;
        worker.postMessage({prefix, address});
    }

    {
        const prefix = "start";
        let renderSelect = document.getElementById("rendererSelect");
        let index = renderSelect.selectedIndex;
        let value = renderSelect.options[index].value;
        worker.postMessage({ prefix, "rendererName":value, canvas }, [canvas]);
    }
}
