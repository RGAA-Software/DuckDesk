import { createI18n } from 'vue-i18n' //引入vue-i18n组件
import messages from './index'

const lang = localStorage.getItem('language') || 'zh'

const i18n = createI18n({
  silentTranslationWarn: true,  //

  //关闭警告信息
  globalInjection: true,        //是否开启全局
  legacy: false, // you must specify 'legacy: false' option
  locale: lang,        //当前的语言
  messages,      //语言文件
});


export default i18n
