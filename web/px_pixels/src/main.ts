import { createApp } from 'vue'
import Antd from 'ant-design-vue'
import 'ant-design-vue/dist/reset.css'
/* 像素字体（Press Start 2P，OFL 开源许可）—— 仅打包 latin 子集，随项目构建产物部署 */
import '@fontsource/press-start-2p/latin-400.css'
import App from './App.vue'
import { i18n } from './i18n'
import './styles/variables.css'
import './styles/base.css'

createApp(App).use(Antd).use(i18n).mount('#app')
