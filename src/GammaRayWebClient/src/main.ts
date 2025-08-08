import './assets/main.css'

import { createApp } from 'vue'
import { createPinia } from 'pinia'

import App from './App.vue'
import router from './router'
import PrimeVue from "primevue/config"; // 导入 PrimeVue 插件
import Aura from "@primevue/themes/aura"; // 导入 Aura 主题
import { definePreset } from '@primeuix/themes';

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

const MyPreset = definePreset(Aura, {
    semantic: {
        primary: {
            50: '{blue.50}',
            100: '{blue.100}',
            200: '{blue.200}',
            300: '{blue.300}',
            400: '{blue.400}',
            500: '{blue.500}',
            600: '{blue.600}',
            700: '{blue.700}',
            800: '{blue.800}',
            900: '{blue.900}',
            950: '{blue.950}'
        }
    }
});

app.use(PrimeVue, {
    theme: {
        //preset: Aura, // 使用 Aura 主题
        preset: MyPreset, // 使用 Aura 主题
    },
});

app.mount('#app')

//
const grApp = new GrApp();
grApp.start();