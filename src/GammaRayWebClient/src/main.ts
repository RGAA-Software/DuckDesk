import './assets/main.css'

import { createApp } from 'vue'
import { createPinia } from 'pinia'

import App from './App.vue'
import router from './router'
import PrimeVue from "primevue/config"; // 导入 PrimeVue 插件
import Aura from "@primevue/themes/aura"; // 导入 Aura 主题

import protobuf from 'protobufjs'
import { GrProtoMsg, loadMessageType } from '@/messages/gr_proto_messages.ts'
import { GrApp } from '@/gr_app.ts'

const app = createApp(App)

const root = await protobuf.load([
    'proto/tc_file_transfer.proto',
    'proto/tc_signaling_message.proto',
    'proto/tc_message.proto',
])
loadMessageType(root)

const hello = GrProtoMsg.MsgHello.create({
    enable_video: true,
    enable_audio: true,
})
hello.enable_audio = true
console.log(`load proto success:`, hello)

app.use(createPinia())
app.use(router)
app.use(PrimeVue, {
    theme: {
        preset: Aura, // 使用 Aura 主题
    },
});

app.mount('#app')

//
const grApp = new GrApp();
grApp.start();