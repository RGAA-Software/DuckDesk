<script setup lang="ts">
import cloudIcon from '@/assets/icon/ic_cloud.svg'
import downloadIcon from '@/assets/icon/ic_download.svg'
import preview1 from '@/assets/preview/preview-1.png'
import preview2 from '@/assets/preview/preview-2.png'
import preview3 from '@/assets/preview/preview-3.png'
import preview4 from '@/assets/preview/preview-4.png'

import webInfoMain from '@/assets/main/ic_new_experience.svg'
import webInfoFileTransfer from '@/assets/main/ic_file_transfer.svg'
import webInfoSecurity from '@/assets/main/ic_private.svg'
import webInfoConfigurable from '@/assets/main/ic_custmize.svg'
import webInfoFast from '@/assets/main/ic_speed.svg'
import checkOk from '@/assets/icon/ic_check_ok.svg'
import heartIcon from '@/assets/icon/ic_heart.svg'
import resGame from '@/assets/main/res-game.jpg'
import resFinancial from '@/assets/main/res-financial.jpg'
import resEnergy from '@/assets/main/res-energy.jpg'
import resArchitecture from '@/assets/main/res-architecture.jpg'
import resMedical from '@/assets/main/res-medical.jpg'
import resCatering from '@/assets/main/res-catering.jpg'
import resEducation from '@/assets/main/res-education.jpg'
import resSupport from '@/assets/main/res-support.jpg'
import BottomDecoratorDivider from '@/components/BottomDecoratorDivider.vue'
import emailIcon from '@/assets/icon/ic_email.svg'
import writeIcon from '@/assets/icon/ic_write.svg'
import cnFlag from '@/assets/icon/ic_cn_flag.svg'
import usFlag from '@/assets/icon/ic_us_flag.svg'
import { useRouter } from 'vue-router'
import ContactUs from '@/components/ContactUs.vue'
import { ref } from 'vue'
import Globe from '@/components/Globe.vue'

const router = useRouter()

// 定义图片数组
const imageList = [preview1, preview4, preview2, preview3]

function downloadKuaKe() {
  window.open('https://pan.quark.cn/s/bfe11452992b', '_blank')
}

function downloadGithub() {
  window.open('https://github.com/RGAA-Software/GammaRay/releases', '_blank')
}

function goToPrice() {
  router.push('/price')
}

const contactUsVisible = ref(false)
const goContactUs = () => {
  contactUsVisible.value = true
}
</script>

<template>
  <!-- Contact us -->
  <ContactUs v-model="contactUsVisible" />

  <div>
    <div
      class="bg-background relative flex size-full min-h-120 flex-col items-center justify-center overflow-hidden px-40 pt-8 pb-40 md:pb-60"
    >
      <span
        class="pointer-events-none bg-linear-to-b from-black to-gray-300/80 bg-clip-text text-center text-8xl leading-none font-semibold whitespace-pre-wrap text-transparent max-lg:-mt-12 dark:from-white dark:to-slate-900/10"
      >
        GoDesk
      </span>
      <Globe class="top-28" />
      <!--        <div-->
      <!--            class="pointer-events-none absolute inset-0 h-full bg-[radial-gradient(circle_at_50%_200%,rgba(0,0,0,0.2),rgba(255,255,255,0))]"-->
      <!--        />-->
    </div>
  </div>

  <div class="h-14"></div>

  <div class="bg-white flex justify-center">
    <el-text class="!text-6xl font-bold !text-slate-800">极速</el-text>
    <el-text class="!text-3xl font-bold !text-slate-800">•</el-text>
    <el-text class="!text-6xl font-bold !text-blue-500">专注</el-text>
    <el-text class="!text-3xl font-bold !text-blue-500">•</el-text>
    <el-text class="!text-6xl font-bold !text-slate-800">远程办公和访问</el-text>
  </div>

  <div class="h-4"></div>

  <div class="bg-white flex justify-center">
    <el-text class="!text-xl !text-slate-700"
      >安全可靠,丝滑流畅,数据完全自主可控的远程办公和访问体验</el-text
    >
  </div>

  <div class="h-8"></div>

  <!--  <div class="bg-white flex justify-center">-->
  <!--    <img-->
  <!--      style="width: 760px"-->
  <!--      src="../assets/main/web-main.jpg"-->
  <!--    />-->
  <!--  </div>-->

  <!--  <div class="bg-white flex justify-center">-->
  <!--    <TopDecoratorDivider />-->
  <!--  </div>-->

  <div class="h-6"></div>

  <div class="bg-white flex justify-center">
    <el-popover placement="right" :width="235" trigger="click">
      <template #reference>
        <el-button type="primary" class="w-32 !h-10" round>
          <el-image
            :src="downloadIcon"
            fit="cover"
            style="width: 20px; height: 20px; cursor: pointer"
          ></el-image>
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

    <el-button type="primary" @click="goToPrice" class="w-32 !h-10" round>
      <el-image
        :src="cloudIcon"
        fit="cover"
        style="width: 20px; height: 20px; cursor: pointer"
      ></el-image>
      <div class="w-1"></div>
      <span>自我部署</span>
    </el-button>
  </div>

  <div class="h-12"></div>

  <div class="bg-white flex justify-center">
    <!-- 1120 630 -->
    <el-carousel :interval="5000" arrow="always" class="custom-carousel" height="745px">
      <el-carousel-item v-for="(image, index) in imageList" :key="index">
        <div class="w-full h-full bg-white rounded-xl border border-gray-400 overflow-hidden">
          <el-image :src="image" class="w-full h-full" fit="cover" />
        </div>
      </el-carousel-item>
    </el-carousel>
  </div>

  <div class="h-12"></div>

  <!-- MAIN -->
  <div class="flex justify-center">
    <div shadow="hover" :body-style="{ padding: '0px' }">
      <div class="flex justify-center">
        <div class="w-140 h-105">
          <div class="h-13"></div>
          <el-text class="!text-5xl font-bold !text-slate-800 !pl-13">全新体验</el-text>
          <div class="h-5"></div>
          <div class="h-10 flex items-center">
            <div class="!pl-13" />
            <el-image :src="checkOk" class="!w-7 !h-7"></el-image>
            <el-text class="!pl-2 !text-xl font-bold !text-slate-700">高清画质</el-text>
          </div>
          <div>
            <el-text class="!pl-22 !text-base font-normal"
              >多屏同显, 高至8K 4:4:4 60帧的全彩体验</el-text
            >
          </div>

          <div class="h-5"></div>
          <div class="h-10 flex items-center">
            <div class="!pl-13" />
            <el-image :src="checkOk" class="!w-7 !h-7"></el-image>
            <el-text class="!pl-2 !text-xl font-bold !text-slate-700">体验UP</el-text>
          </div>
          <div>
            <el-text class="!pl-22 !text-base font-normal">直连中转双网+RTC, 连接更流畅</el-text>
          </div>

          <div class="h-5"></div>
          <div class="h-10 flex items-center">
            <div class="!pl-13" />
            <el-image :src="checkOk" class="!w-7 !h-7"></el-image>
            <el-text class="!pl-2 !text-xl font-bold !text-slate-700">坚实稳定</el-text>
          </div>
          <div>
            <el-text class="!pl-22 !text-base font-normal"
              >持久稳定不卡顿, 字体清晰, 响应精准</el-text
            >
          </div>
        </div>

        <div class="w-140 h-105 flex justify-center">
          <el-image :src="webInfoMain" class="!w-90 !h-90"></el-image>
        </div>
      </div>
    </div>
  </div>

  <div class="h-12"></div>

  <div class="flex justify-center">
    <div>
      <div class="flex justify-center">
        <div class="w-140 h-105 flex justify-center">
          <el-image :src="webInfoFileTransfer" class="!w-90 !h-90"></el-image>
        </div>

        <div class="w-30"></div>

        <div class="w-140 h-100">
          <div class="h-13"></div>
          <el-text class="!text-5xl font-bold !text-slate-800 !pl-13">文件传输</el-text>
          <div class="h-5"></div>
          <div class="h-10 flex items-center">
            <div class="!pl-13" />
            <el-image :src="checkOk" class="!w-7 !h-7"></el-image>
            <el-text class="!pl-2 !text-xl font-bold !text-slate-700">快速稳定</el-text>
          </div>
          <div>
            <el-text class="!pl-22 !text-base font-normal">单个, 批量快速双向传输</el-text>
          </div>

          <div class="h-5"></div>
          <div class="h-10 flex items-center">
            <div class="!pl-13" />
            <el-image :src="checkOk" class="!w-7 !h-7"></el-image>
            <el-text class="!pl-2 !text-xl font-bold !text-slate-700">权限可控</el-text>
          </div>
          <div>
            <el-text class="!pl-22 !text-base font-normal"
              >文件传输权限隔离, 安全稳定不混乱</el-text
            >
          </div>

          <div class="h-5"></div>
          <div class="h-10 flex items-center">
            <div class="!pl-13" />
            <el-image :src="checkOk" class="!w-7 !h-7"></el-image>
            <el-text class="!pl-2 !text-xl font-bold !text-slate-700">操作简洁</el-text>
          </div>
          <div>
            <el-text class="!pl-22 !text-base font-normal">右键唤出菜单即可直接粘贴</el-text>
          </div>
        </div>
      </div>
    </div>
  </div>

  <div class="h-12"></div>

  <div class="flex justify-center">
    <div shadow="hover" :body-style="{ padding: '0px' }">
      <div class="flex justify-center">
        <div class="w-140 h-120">
          <div class="h-11"></div>
          <el-text class="!text-5xl font-bold !text-slate-800 !pl-13">自主安全</el-text>
          <div class="h-5"></div>
          <div class="h-10 flex items-center">
            <div class="!pl-13" />
            <el-image :src="checkOk" class="!w-7 !h-7"></el-image>
            <el-text class="!pl-2 !text-xl font-bold !text-slate-700">数据主权</el-text>
          </div>
          <div>
            <el-text class="!pl-22 !text-base font-normal"
              >独立部署到您的机房, 数据完全可控</el-text
            >
          </div>

          <div class="h-5"></div>
          <div class="h-10 flex items-center">
            <div class="!pl-13" />
            <el-image :src="checkOk" class="!w-7 !h-7"></el-image>
            <el-text class="!pl-2 !text-xl font-bold !text-slate-700">操作日志</el-text>
          </div>
          <div>
            <el-text class="!pl-22 !text-base font-normal"
              >访问, 文件传输全记录, 安全无小事</el-text
            >
          </div>

          <div class="h-5"></div>
          <div class="h-10 flex items-center">
            <div class="!pl-13" />
            <el-image :src="checkOk" class="!w-7 !h-7"></el-image>
            <el-text class="!pl-2 !text-xl font-bold !text-slate-700">设备监控</el-text>
          </div>
          <div>
            <el-text class="!pl-22 !text-base font-normal">无痕查看管理每台设备的工作状况</el-text>
          </div>

          <div class="h-5"></div>
          <div class="h-10 flex items-center">
            <div class="!pl-13" />
            <el-image :src="checkOk" class="!w-7 !h-7"></el-image>
            <el-text class="!pl-2 !text-xl font-bold !text-slate-700">屏幕录制</el-text>
          </div>
          <div>
            <el-text class="!pl-22 !text-base font-normal">随时录制您的操作</el-text>
          </div>
        </div>
        <div class="w-140 h-120 flex justify-center">
          <el-image :src="webInfoSecurity" class="!w-90 !h-90"></el-image>
        </div>
      </div>
    </div>
  </div>

  <div class="h-12"></div>

  <div class="flex justify-center">
    <div shadow="hover" :body-style="{ padding: '0px' }">
      <div class="flex justify-center">
        <div class="w-140 h-105 flex justify-center">
          <el-image :src="webInfoConfigurable" class="!w-90 !h-90"></el-image>
        </div>

        <div class="w-30"></div>

        <div class="w-140 h-100">
          <div class="h-13"></div>
          <el-text class="!text-5xl font-bold !text-slate-800 !pl-13">高级定制</el-text>

          <div class="h-5"></div>
          <div class="h-10 flex items-center">
            <div class="!pl-13" />
            <el-image :src="checkOk" class="!w-7 !h-7"></el-image>
            <el-text class="!pl-2 !text-xl font-bold !text-slate-700">按需修改</el-text>
          </div>
          <div>
            <el-text class="!pl-22 !text-base font-normal">多项全面的配置供您选择</el-text>
          </div>

          <div class="h-5"></div>
          <div class="h-10 flex items-center">
            <div class="!pl-13" />
            <el-image :src="heartIcon" class="!w-7 !h-7"></el-image>
            <el-text class="!pl-2 !text-xl font-bold !text-slate-700">联系我们定制方案</el-text>
          </div>
          <div>
            <el-text class="!pl-22 !text-base font-bold !text-slate-700"
              >godesk-sales@outlook.com</el-text
            >
          </div>
        </div>
      </div>
    </div>
  </div>

  <div class="h-12"></div>

  <div class="flex justify-center">
    <div shadow="hover" :body-style="{ padding: '0px' }">
      <div class="flex justify-center">
        <div class="w-140 h-105">
          <div class="h-13"></div>
          <el-text class="!text-5xl font-bold !text-slate-800 !pl-13">性能拉满</el-text>
          <div class="h-5"></div>
          <div class="h-10 flex items-center">
            <div class="!pl-13" />
            <el-image :src="checkOk" class="!w-7 !h-7"></el-image>
            <el-text class="!pl-2 !text-xl font-bold !text-slate-700">硬件加速</el-text>
          </div>
          <div>
            <el-text class="!pl-22 !text-base font-normal"
              >AMD Nvidia硬编码与全面,全彩(4:4:4)的硬解码</el-text
            >
          </div>

          <div class="h-5"></div>
          <div class="h-10 flex items-center">
            <div class="!pl-13" />
            <el-image :src="checkOk" class="!w-7 !h-7"></el-image>
            <el-text class="!pl-2 !text-xl font-bold !text-slate-700">算法优化</el-text>
          </div>
          <div>
            <el-text class="!pl-22 !text-base font-normal">精准调控与数据控制到达</el-text>
          </div>

          <div class="h-5"></div>
          <div class="h-10 flex items-center">
            <div class="!pl-13" />
            <el-image :src="checkOk" class="!w-7 !h-7"></el-image>
            <el-text class="!pl-2 !text-xl font-bold !text-slate-700">资源效率</el-text>
          </div>
          <div>
            <el-text class="!pl-22 !text-base font-normal"
              >渲染效率高, 资源占用低, 可放心运行您的大型三维程序</el-text
            >
          </div>
        </div>
        <div class="w-140 h-105 flex justify-center">
          <el-image :src="webInfoFast" class="!w-90 !h-90"></el-image>
        </div>
      </div>
    </div>
  </div>

  <div class="h-12"></div>

  <div class="flex justify-center">
    <el-text class="!text-3xl font-bold !text-slate-800">您的行业解决方案</el-text>
  </div>

  <div class="h-10"></div>
  <div class="flex justify-center">
    <el-card :body-style="{ padding: '0px' }" shadow="hover" class="">
      <el-image :src="resGame" class="card-image"></el-image>
      <div class="flex justify-center pt-4 pb-4">
        <span class="!text-slate-700 font-bold text-lg">游戏/动画/影视</span>
      </div>
    </el-card>

    <div class="w-12"></div>
    <el-card :body-style="{ padding: '0px' }" shadow="hover">
      <el-image :src="resEducation" class="card-image"></el-image>
      <div class="flex justify-center pt-4 pb-4">
        <span class="!text-slate-700 font-bold text-lg">教育</span>
      </div>
    </el-card>

    <div class="w-12"></div>
    <el-card :body-style="{ padding: '0px' }" shadow="hover">
      <el-image :src="resFinancial" class="card-image"></el-image>
      <div class="flex justify-center pt-4 pb-4">
        <span class="!text-slate-700 font-bold text-lg">金融</span>
      </div>
    </el-card>

    <div class="w-12"></div>
    <el-card :body-style="{ padding: '0px' }" shadow="hover">
      <el-image :src="resEnergy" class="card-image"></el-image>
      <div class="flex justify-center pt-4 pb-4">
        <span class="!text-slate-700 font-bold text-lg">能源/矿产</span>
      </div>
    </el-card>
  </div>

  <div class="h-12"></div>
  <div class="flex justify-center">
    <el-card :body-style="{ padding: '0px' }" shadow="hover" class="">
      <el-image :src="resArchitecture" class="card-image"></el-image>
      <div class="flex justify-center pt-4 pb-4">
        <span class="!text-slate-700 font-bold text-lg">建筑</span>
      </div>
    </el-card>

    <div class="w-12"></div>
    <el-card :body-style="{ padding: '0px' }" shadow="hover">
      <el-image :src="resMedical" class="card-image"></el-image>
      <div class="flex justify-center pt-4 pb-4">
        <span class="!text-slate-700 font-bold text-lg">医疗与健康</span>
      </div>
    </el-card>

    <div class="w-12"></div>
    <el-card :body-style="{ padding: '0px' }" shadow="hover">
      <el-image :src="resSupport" class="card-image"></el-image>
      <div class="flex justify-center pt-4 pb-4">
        <span class="!text-slate-700 font-bold text-lg">远程支持</span>
      </div>
    </el-card>

    <div class="w-12"></div>
    <el-card :body-style="{ padding: '0px' }" shadow="hover">
      <el-image :src="resCatering" class="card-image"></el-image>
      <div class="flex justify-center pt-4 pb-4">
        <span class="!text-slate-800 font-bold text-lg">餐饮服务</span>
      </div>
    </el-card>
  </div>

  <div class="h-12"></div>

  <div class="flex justify-center">
    <el-text class="!text-3xl font-bold !text-slate-800">即刻体验企业级安全的远程办公</el-text>
  </div>

  <div class="h-10"></div>

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

    <div class="w-5"></div>

    <div>
      <el-popover placement="top" :width="235" trigger="click">
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

  <div class="h-16" />
  <div class="bg-white flex justify-center">
    <BottomDecoratorDivider />
  </div>
</template>

<style scoped>
.custom-main-carousel {
  width: 100%;
  height: 100%;
  object-fit: cover;
  border: 2px solid #eeeeee;
  border-radius: 5px;
}

.custom-carousel {
  width: 1320px;
  height: 745px;
}

.card-image {
  width: 230px;
  height: 250px;
}
</style>
