<script setup lang="ts">
import cnFlag from '@/assets/icon/ic_cn_flag.svg'
import usFlag from '@/assets/icon/ic_us_flag.svg'
import icWindows from '@/assets/icon/ic_windows.svg'
import icLinux from '@/assets/icon/ic_linux.svg'
import icAndroid from '@/assets/icon/ic_android.svg'
import icIphone from '@/assets/icon/ic_iphone.svg'
import icMacos from '@/assets/icon/ic_mac_os.svg'

function goDocsPage() {
  window.open('https://docs.godesk.uk/', '_blank')
}
</script>

<template>
  <div class="h-10" />

  <div class="bg-white flex justify-center">
    <el-text class="!text-2xl font-bold !text-slate-700">使用文档</el-text>
  </div>

  <div class="h-10"></div>

  <div class="flex !items-center !justify-center">
    <el-card shadow="hover" class="w-100 text-lg !text-slate-700 font-bold">
      <div class="w-full h-10 flex justify-center items-center">
        <el-image :src="cnFlag" fit="cover" class="rounded-full h-8 w-8"></el-image>
        <div class="w-2"></div>
        <el-image :src="usFlag" fit="cover" class="rounded-full h-8 w-8"></el-image>
        <div class="w-2"></div>
        官方网站
      </div>
      <div class="w-full h-20 flex justify-center items-center">
        <el-button type="primary" class="w-40" @click="goDocsPage">访问</el-button>
      </div>
    </el-card>
  </div>

  <div class="h-10"></div>

  <div class="bg-white flex justify-center">
    <el-text class="!text-2xl font-bold !text-slate-700">支持平台</el-text>
  </div>

  <div class="h-10"></div>

  <div class="flex !items-center !justify-center">
    <el-card shadow="hover" class="w-70 !h-70 text-lg !text-slate-700">
      <template #header>
        <div class="font-medium">客户端</div>
      </template>

      <div class="w-full h-10 flex items-center text-base">
        <el-image :src="icWindows" fit="cover" class="h-8 w-8"></el-image>
        <div class="w-2"></div>
        Windows
      </div>

      <div class="h-5"></div>
      <div class="w-full h-10 flex items-center text-base">
        <el-image :src="icAndroid" fit="cover" class="h-8 w-8"></el-image>
        <div class="w-2"></div>
        Android
      </div>

      <!--      <div class="h-5"></div>-->
      <!--      <div class="w-full h-10 flex items-center text-base">-->
      <!--        <el-image-->
      <!--          :src="icMacos"-->
      <!--          fit="cover"-->
      <!--          class="h-8 w-8"-->
      <!--        ></el-image>-->
      <!--        <div class="w-2"></div>-->
      <!--        MacOS(Coming soon)-->
      <!--      </div>-->

      <!--      <div class="h-5"></div>-->
      <!--      <div class="w-full h-10 flex items-center text-base">-->
      <!--        <el-image-->
      <!--          :src="icLinux"-->
      <!--          fit="cover"-->
      <!--          class="h-8 w-8"-->
      <!--        ></el-image>-->
      <!--        <div class="w-2"></div>-->
      <!--        Linux(Coming soon)-->
      <!--      </div>-->

      <!--      <div class="h-5"></div>-->
      <!--      <div class="w-full h-10 flex items-center text-base">-->
      <!--        <el-image-->
      <!--          :src="icIphone"-->
      <!--          fit="cover"-->
      <!--          class="h-8 w-8"-->
      <!--        ></el-image>-->
      <!--        <div class="w-2"></div>-->
      <!--        iOS(Coming soon)-->
      <!--      </div>-->
    </el-card>

    <div class="w-5"></div>

    <el-card shadow="hover" class="w-70 !h-70 text-lg !text-slate-700">
      <template #header>
        <div class="font-medium">管理端</div>
      </template>

      <div class="w-full h-10 flex items-center text-base">
        <el-image :src="icWindows" fit="cover" class="h-8 w-8"></el-image>
        <div class="w-2"></div>
        Windows
      </div>

      <div class="h-5"></div>

      <div class="w-full h-10 flex items-center text-base">
        <el-image :src="icLinux" fit="cover" class="rounded-full h-8 w-8"></el-image>
        <div class="w-2"></div>
        Linux
      </div>
    </el-card>
  </div>
</template>

<style scoped></style>
