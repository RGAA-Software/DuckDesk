<script setup lang="ts">
import checkIcon from '@/assets/icon/ic_check.svg'
import checkOkIcon from '@/assets/icon/ic_check_ok.svg'

import { ref } from 'vue'
import warnIcon from '@/assets/icon/ic_warn.svg'
import writeIcon from '@/assets/icon/ic_write.svg'
import emailIcon from '@/assets/icon/ic_email.svg'
import downloadIcon from '@/assets/icon/ic_download.svg'
import cnFlag from '@/assets/icon/ic_cn_flag.svg'
import usFlag from '@/assets/icon/ic_us_flag.svg'
import ContactUs from '@/components/ContactUs.vue'
import BottomDecoratorDivider from '@/components/BottomDecoratorDivider.vue'

// 定义类型
interface FeatureItem {
  id: number
  desc: string
}

const personal_features = ref<FeatureItem[]>([
  { id: 1, desc: '超清,高至8K' },
  { id: 1, desc: '帧率覆盖15~144 FPS' },
  { id: 1, desc: '可登录用户数不限' },
  { id: 1, desc: '管理设备数不限' },
  { id: 2, desc: '同时控制2台设备' },
  { id: 1, desc: '高速文件传输' },
  { id: 1, desc: '服务端N卡，A卡硬件加速' },
  { id: 1, desc: '画面尺寸自定义' },
  { id: 1, desc: '中继服务' },
  { id: 1, desc: '多屏同显同控' },
  { id: 1, desc: '画面截图, 视频录制' },
  { id: 1, desc: '客户端Vulkan, D3D硬件加速' },
  { id: 1, desc: '全彩4:4:4' },
  { id: 2, desc: '访问历史记录' },
  { id: 2, desc: '文件传输历史记录' },
])

const enterprise_features = ref<FeatureItem[]>([
  { id: 1, desc: '免费定制LOGO/企业名称' },
  { id: 1, desc: '优先升级' },
  { id: 1, desc: '快速响应' },
  { id: 1, desc: '远程协助' },
])

function downloadKuaKe() {
  window.open('https://pan.quark.cn/s/bfe11452992b', '_blank')
}

function downloadGithub() {
  window.open('https://github.com/RGAA-Software/GammaRay/releases', '_blank')
}

const contactUsVisible = ref(false)
const goContactUs = () => {
  contactUsVisible.value = true
}
</script>

<template>
  <div class="h-14"></div>

  <div class="bg-white flex justify-center">
    <el-text class="!text-6xl font-bold !text-blue-500">自我部署</el-text>
    <el-text class="!text-6xl font-bold !text-slate-800">解决方案</el-text>
  </div>

  <div class="h-5"></div>

  <div class="bg-white flex justify-center">
    <el-text class="!text-xl !text-slate-700">免费可用 按需付费</el-text>
  </div>

  <div class="h-4"></div>

  <div class="h-10 flex justify-center items-center">
    <el-image
      :src="warnIcon"
      fit="cover"
      style="width: 22px; height: 22px; cursor: pointer"
    ></el-image>
    <el-text class="!text-xl !pl-1 font-bold !text-slate-700">
      我们不是SaaS服务, 您需要自我部署
    </el-text>
  </div>

  <div class="h-8"></div>

  <div class="flex justify-center">
    <el-card shadow="hover" class="!w-85 flex flex-col">
      <template #header>
        <div class="card-header">
          <span class="font-bold !text-slate-700">个人使用</span>
        </div>
      </template>

      <div class="h-130">
        <p class="!text-lg font-bold text-green-600">0$/年</p>
        <div class="h-3"></div>

        <div v-for="item in personal_features" :key="item.id" class="h-8">
          <div>
            <el-image
              :src="checkIcon"
              fit="cover"
              style="width: 18px; height: 18px; cursor: pointer"
            ></el-image>
            {{ item.desc }}
          </div>
        </div>
      </div>
    </el-card>

    <div class="!w-6"></div>

    <el-card shadow="hover" class="!w-85">
      <template #header>
        <div class="card-header">
          <span class="font-bold !text-slate-700">企业合作</span>
        </div>
      </template>

      <div class="h-130">
        <p class="!text-lg font-bold">69$/席位/年</p>
        <div class="h-3"></div>

        <div v-for="item in enterprise_features" :key="item.id" class="h-8">
          <div>
            <el-image
              :src="checkIcon"
              fit="cover"
              style="width: 18px; height: 18px; cursor: pointer"
            ></el-image>
            {{ item.desc }}
          </div>
        </div>

        <p class="!text-base font-bold">+全部功能解锁</p>
      </div>

      <!--      <div class="flex justify-center">-->
      <!--        <el-button type="warning" class="w-40 !h-10" round>联系我们</el-button>-->
      <!--      </div>-->
    </el-card>
  </div>

  <div class="h-20"></div>

  <div class="flex justify-center">
    <BottomDecoratorDivider />
  </div>
  <div class="h-5"></div>

  <div class="flex justify-center">
    <el-popover placement="top" :width="235" trigger="click">
      <template #reference>
        <el-button type="primary" class="w-40 !h-10" round>
          <el-image
            :src="downloadIcon"
            fit="cover"
            style="width: 20px; height: 20px; cursor: pointer"
          ></el-image>
          <div class="w-1"></div>
          <span>下载</span>
        </el-button>
      </template>

      <div class="w-full h-30 flex flex-col justify-center items-start">
        <div class="flex pl-5">
          <el-image :src="cnFlag" fit="cover" class="rounded-full w-8 h-8"></el-image>
          <div class="w-3"></div>
          <el-button @click="downloadKuaKe">下载(线路1)</el-button>
        </div>

        <div class="h-4"></div>

        <div class="flex pl-5">
          <el-image :src="usFlag" fit="cover" class="rounded-full w-8 h-8"></el-image>
          <div class="w-3"></div>
          <el-button @click="downloadGithub">下载(线路2)</el-button>
        </div>
      </div>
    </el-popover>

    <div class="w-5"></div>

    <div class="bg-white flex justify-center">
      <el-button type="primary" class="w-40 !h-10" round @click="goContactUs">
        <el-image
          :src="writeIcon"
          fit="cover"
          style="width: 20px; height: 20px; cursor: pointer"
        ></el-image>
        <div class="w-1"></div>
        <span>给我们留言</span>
      </el-button>
    </div>

    <div class="w-5"></div>

    <div>
      <el-popover placement="top" :width="265" trigger="click">
        <template #reference>
          <el-button type="primary" class="w-40 !h-10" round>
            <el-image
              :src="emailIcon"
              fit="cover"
              style="width: 20px; height: 20px; cursor: pointer"
            ></el-image>
            <div class="w-1"></div>
            <span>给我们发邮件</span>
          </el-button>
        </template>

        <div class="w-full h-10 flex flex-col justify-center items-center">
          <el-text class="!text-base !font-bold !text-slate-700">godesk-sales@outlook.com</el-text>
        </div>
      </el-popover>
    </div>
  </div>

  <ContactUs v-model="contactUsVisible" />
</template>
