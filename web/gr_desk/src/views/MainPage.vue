<template>
  <!--  #2979ff-->
  <!--  <div class="h-12 flex justify-center items-center bg-orange-100">-->
  <!--    <el-image-->
  <!--      :src="warnIcon"-->
  <!--      fit="cover"-->
  <!--      style="width: 22px; height: 22px; cursor: pointer"-->
  <!--    ></el-image>-->
  <!--    <el-text class="!text-xl !pl-1 font-bold !text-gray-500">-->
  <!--      www.godesk.uk{{ i18n.t('message.OnlyOfficialWebsite') }}-->
  <!--    </el-text>-->
  <!--  </div>-->

  <el-row>
    <el-col :span="4">
      <div class="bg-white h-13 flex items-center justify-center">
        <el-image
          :src="logoIcon"
          fit="cover"
          style="width: 10rem; cursor: pointer"
          @click="handleClickLogo"
        ></el-image>
      </div>
    </el-col>

    <el-col :span="16">
      <div class="bg-white h-13">
        <el-menu
          :default-active="activeIndex"
          class="!h-13 custom-menu flex justify-center"
          mode="horizontal"
          @select="handleSelect"
          router
        >
          <el-menu-item
            index="/main"
            class="w-30 !text-base font-normal hover:font-bold [&.is-active]:font-bold"
            @click="handleClickMainPage"
            >{{ i18n.t('message.Home') }}
          </el-menu-item>

          <el-menu-item
            index="/price"
            class="w-30 !text-base font-normal hover:font-bold [&.is-active]:font-bold"
            @click="handleClickPricePage"
            >{{ i18n.t('message.Price') }}
          </el-menu-item>

          <el-menu-item
            index="/docs"
            class="w-30 !text-base font-normal hover:font-bold [&.is-active]:font-bold"
            >{{ i18n.t('message.Docs') }}
          </el-menu-item>
        </el-menu>
      </div>
    </el-col>

    <el-col :span="4">
      <!-- bg-gray-200 -->
      <div class="bg-white h-13 flex items-center">
        <div class="flex justify-end w-full items-center">
          <el-dropdown trigger="click" @command="handleTranslateClick">
            <el-image
              :src="transIcon"
              fit="cover"
              style="width: 20px; height: 20px; cursor: pointer"
            ></el-image>

            <template v-slot:dropdown>
              <el-dropdown-menu>
                <el-dropdown-item command="zh">简体中文</el-dropdown-item>
                <el-dropdown-item command="en">English</el-dropdown-item>
              </el-dropdown-menu>
            </template>
          </el-dropdown>

          <div class="w-10"></div>

          <el-image
            :src="steamIcon"
            fit="cover"
            style="width: 30px; height: 30px; cursor: pointer"
            @click="handleSteamClick"
          ></el-image>

          <div class="w-4"></div>

          <el-image
            :src="githubIcon"
            fit="cover"
            style="width: 30px; height: 30px; cursor: pointer"
            @click="handleGithubClick"
          ></el-image>

          <div class="w-4"></div>
        </div>
      </div>
    </el-col>
  </el-row>

  <el-main class="!pl-0 !pr-0">
    <RouterView />
  </el-main>

  <div class="h-12" />
  <el-divider />
  <div class="h-60 flex justify-center">
    <div style="width: 80px"></div>
    <div class="w-80">
      <div class="h-5"></div>
      <div class="flex justify-center">
        <el-image
          :src="iconLogo"
          fit="cover"
          style="width: 60px; height: 60px; cursor: pointer"
        ></el-image>
        <div class="w-1" />
        <div>
          <div class="!text-2xl font-bold !h-8 !text-slate-800">GoDesk</div>
          <div class="bg-blue-300 h-3">
            <el-text class="!text-medium font-medium !text-white">Always Online</el-text>
          </div>
          <div class="bg-blue-400 h-3" />
        </div>
      </div>
      <div class="h-5"></div>
      <div class="flex justify-center">
        <el-link class="h-6" !text-slate-700 @click="goTerms">条款</el-link>
        <div class="w-3" />
        <el-link class="h-6" !text-slate-700 @click="goPrivacy">隐私政策</el-link>
      </div>
    </div>

    <div class="w-30" />
    <div class="w-45">
      <div class="h-5"></div>
      <div class="">
        <div class="font-medium !text-slate-800">合作与支持</div>
        <div class="h-1" />
        <el-link type="primary" class="h-6" @click="goContactUs">联系我们</el-link>
        <p />
        <el-link class="h-6 !text-slate-700" @click="goHelp">帮助中心</el-link>
        <p />
        <el-link class="h-6 !text-slate-700" @click="goIssue">提交工单</el-link>
        <p />
        <!--        <el-link class="h-6 !text-slate-700" @click="">关于GoDesk</el-link> <p/>-->
      </div>
    </div>

    <ContactUs v-model="contactUsVisible" />

    <!--  -->
    <el-dialog v-model="issueVisible" :modal="false" modal-penetrable align-center>
      <template #header>
        <el-text class="!text-lg !text-slate-700">请填写您的问题</el-text>
      </template>

      <el-form :model="issue" label-width="auto" style="max-width: 600px">
        <el-form-item label="您的问题*">
          <el-input v-model="issue.title" />
        </el-form-item>

        <el-form-item label="怎么称呼您*">
          <el-input v-model="issue.yourName" />
        </el-form-item>

        <el-form-item label="详细内容*">
          <el-input
            v-model="issue.desc"
            :rows="2"
            type="textarea"
            placeholder="请输入您想要咨询的内容"
          />
        </el-form-item>

        <el-form-item label="软件版本">
          <el-input v-model="issue.version" />
        </el-form-item>

        <el-form-item label="操作系统版本">
          <el-input v-model="issue.os" />
        </el-form-item>

        <el-form-item label="邮件">
          <el-input v-model="issue.email" />
        </el-form-item>

        <el-form-item label="微信">
          <el-input v-model="issue.wechat" />
        </el-form-item>

        <el-form-item label="QQ">
          <el-input v-model="issue.qq" />
        </el-form-item>
      </el-form>

      <template #footer>
        <div class="dialog-footer">
          <el-button @click="issueVisible = false">取消</el-button>
          <el-button type="primary" @click="confirmIssue"> 提交 </el-button>
        </div>
      </template>
    </el-dialog>
  </div>
</template>

<script lang="ts" setup>
import { computed, ref, warn } from 'vue'
import githubIcon from '@/assets/ic_github.svg'
import steamIcon from '@/assets/ic_steam.svg'
import transIcon from '@/assets/icon/ic_translate.svg'
import logoIcon from '@/assets/tc_logo_text_white_bg.png'
import warnIcon from '@/assets/icon/ic_warn.svg'
import iconLogo from '@/assets/icon/ic_trans_icon_blue.png'
import emailIcon from '@/assets/icon/ic_email.svg'
import githubLogo from '@/assets/icon/ic_github.svg'
import bilibiliLogo from '@/assets/icon/ic_bilibili.svg'
import steamLogo from '@/assets/icon/ic_steam.svg'
import tiktokLogo from '@/assets/icon/ic_tiktok.svg'
import youtubeLogo from '@/assets/icon/ic_youtube.svg'
// 国际化
import { useI18n } from 'vue-i18n'
import { useRoute, useRouter } from 'vue-router'
import ContactUs from '@/components/ContactUs.vue'
import axios from 'axios'
import { ElNotification } from 'element-plus'
import axiosHttp from '@/http.ts'

const i18n = useI18n()
const router = useRouter()
const route = useRoute()

const contactUsVisible = ref(false)
const issueVisible = ref(false)

const handleClickLogo = () => {
  console.log('main page clicked')
  router.push('/main')
}

const handleClickMainPage = () => {
  console.log('main page clicked')
}

const handleClickPricePage = () => {
  console.log('download clicked')
}

const handleGithubClick = () => {
  console.log('github clicked')
  window.open('https://github.com/RGAA-Software/GammaRay', '_blank')
}

const handleSteamClick = () => {
  console.log('steam-clicked')
}

const handleTranslateClick = (command: string) => {
  console.log('translate-clicked', command)
  localStorage.setItem('language', command)
  i18n.locale.value = localStorage.getItem('language') || 'zh'
}

const activeIndex = computed(() => {
  // 如果是 /docs 或 /docs/ 开头的路径
  if (route.path.startsWith('/docs')) {
    return '/docs'
  }
  // 如果是 /price 或 /price/ 开头的路径
  if (route.path.startsWith('/price')) {
    return '/price'
  }
  // 默认返回当前路径或首页
  return route.path || '/main'
})

const handleSelect = (key: string, keyPath: string[]) => {
  console.log(key, keyPath)
}

const goTerms = () => {
  router.push('/terms')
}

const goPrivacy = () => {
  router.push('/privacy')
}

const goContactUs = () => {
  contactUsVisible.value = true
}

const goHelp = () => {
  router.push('/docs')
}

const goIssue = () => {
  issueVisible.value = true
}

interface Issue {
  title: string
  yourName: ''
  desc: string
  version: string
  os: string
  email: string
  wechat: string
  qq: string
}

const issue = ref<Issue>({
  title: '',
  yourName: '',
  desc: '',
  version: '',
  os: '',
  email: '',
  wechat: '',
  qq: '',
})

async function confirmIssue() {
  issueVisible.value = false
  await postIssue()
}

async function postIssue() {
  try {
    const { data } = await axiosHttp.post(
      '/api/v1/create/new/issue',
      {
        title: issue.value.title,
        your_name: issue.value.yourName,
        desc: issue.value.desc,
        version: issue.value.version,
        os: issue.value.os,
        email: issue.value.email,
        wechat: issue.value.wechat,
        qq: issue.value.wechat,
      },
      {
        headers: {
          'Content-Type': 'application/json',
        },
      },
    )
    ElNotification({
      title: '提交成功',
      message: '已收到您的咨询, 我们会尽快回复',
      type: 'primary',
    })
  } catch (error) {
    console.log('post issue failed: ', error)
    ElNotification({
      title: '提交失败',
      message: '请填写必要信息后再提交',
      type: 'warning',
    })
  }
}
</script>

<style scoped>
.custom-menu {
  border-bottom: none !important;
}

:deep(.el-menu-item) {
  border-bottom: none !important;
}

:deep(.custom-menu .el-menu-item:hover) {
  background-color: transparent !important;
  border-bottom: none !important;
}

:deep(.custom-menu .el-menu-item.is-active) {
  background-color: transparent !important;
  border-bottom: none !important;
}
</style>
