import './assets/main.css'

import { createApp } from 'vue'
import { createPinia } from 'pinia'

import App from './App.vue'
import router from './router'

import protobuf from 'protobufjs'
import { loadMessageType, ProtoMessage } from './proto_messages.ts'

const app = createApp(App)

const root = await protobuf.load(['proto/tc_file_transfer.proto', 'proto/tc_signaling_message.proto', 'proto/tc_message.proto'])
loadMessageType(root)

const hello = ProtoMessage.MsgHello.create({
  enable_video: true,
  enable_audio: true,
});
hello.enable_audio = true
console.log(`load proto success:`, hello)

app.use(createPinia())
app.use(router)

app.mount('#app')
