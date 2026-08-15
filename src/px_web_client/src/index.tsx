/**
 * This is an auto-generated demo by dumi
 * if you think it is not working as expected,
 * please report the issue at
 * https://github.com/umijs/dumi/issues
 */

import React from 'react';
import protobuf from 'protobufjs'
import { createRoot } from "react-dom/client";
import App from "./App";
import {GrApp} from "./px_app.ts";
import {GrProtoMsg, loadMessageType} from "./messages/px_proto_messages.ts";

const protoRoot = await protobuf.load([
    'proto/px_file_transfer.proto',
    'proto/px_signaling_message.proto',
    'proto/px_message.proto',
])
loadMessageType(protoRoot)

const hello = GrProtoMsg.MsgHello.create({
    enable_video: true,
    enable_audio: true,
})
hello.enable_audio = true
console.log(`load proto success:`, hello)

const rootElement = document.getElementById("root");
const root = createRoot(rootElement);

// root.render(<App />);

const grApp = new GrApp();
grApp.start();