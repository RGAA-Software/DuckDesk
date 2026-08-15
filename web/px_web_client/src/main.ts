import { createApp } from 'vue'
import ElementPlus from 'element-plus'
import 'element-plus/dist/index.css'
import App from './App.vue'
import { i18n, applyDocumentTitle } from './locales/i18n'

applyDocumentTitle()

createApp(App).use(i18n).use(ElementPlus).mount('#app')
