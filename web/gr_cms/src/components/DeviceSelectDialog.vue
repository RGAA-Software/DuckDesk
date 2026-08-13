<script setup lang="ts">
import { computed, ref, watch } from 'vue'
import { notification } from 'ant-design-vue'
import { queryDevices } from '@/model/device_api.ts'
import type { Device } from '@/entity/device.ts'
import { formatTimestamp } from '@/util/time.ts'

interface Props {
  title: string
  modelValue: boolean
}

const props = withDefaults(defineProps<Props>(), {
  title: '搜索设备',
  modelValue: false,
})

// 定义 emits
const emit = defineEmits<{
  (e: 'update:modelValue', value: boolean): void
  (e: 'select', device: Device): void
}>()

const dialogVisible = computed({
  get: () => props.modelValue,
  set: (value) => emit('update:modelValue', value),
})

// const handleCancel = async () => {
//   emit('update:modelValue', false)
//   emit('cancel')
// }
//
// const handleConfirm = async () => {
//   emit('update:modelValue', false)
//   emit('confirm')
// }

const pageSize = ref(20)
const currentPage = ref(1)
const size = ref<'small' | 'default'>('default')
const disabled = ref(false)

const searchDeviceName = ref<string>('')
const searchDeviceCode = ref<string>('')
const searchDeviceIp = ref<string>('')
const searchOnlineState = ref<string>('')
const searchOnlineOptions = [
  {
    value: 'all',
    label: '全部',
  },
  {
    value: 'online',
    label: '在线',
  },
  {
    value: 'offline',
    label: '离线',
  },
]

const devices = ref<Device[]>([])

const handleSearchDevices = async () => {
  console.log(
    'search, device name: ',
    searchDeviceName.value,
    ', device id: ',
    searchDeviceCode.value,
    ', ip: ',
    searchDeviceIp.value,
  )
  devices.value = await queryDevices(
    searchDeviceName.value,
    searchDeviceCode.value,
    searchDeviceIp.value,
    searchOnlineState.value,
    currentPage.value,
    pageSize.value,
  )
}

// 打开弹窗时默认拉取设备列表（默认 20 分页）
watch(
  () => props.modelValue,
  (visible) => {
    if (visible) {
      currentPage.value = 1
      pageSize.value = 20
      handleSearchDevices()
    }
  },
)

const handleClearDevices = async () => {
  searchDeviceName.value = ''
  searchDeviceCode.value = ''
  searchDeviceIp.value = ''
  devices.value = []
}

const handleSelectDevice = async (index: number, device: Device) => {
  console.log('index: ', index, ', device: ', device)

  if (!device.online) {
    notification.warning({
      message: '此设备: ' + device.device_name + ' 已经离线',
    })
    return
  }

  emit('select', device)

  // 顺便关闭 dialog
  emit('update:modelValue', false)
}

const handlePageChange = async (page: number, size: number) => {
  currentPage.value = page
  pageSize.value = size
  devices.value = await queryDevices(
    searchDeviceName.value,
    searchDeviceCode.value,
    searchDeviceIp.value,
    searchOnlineState.value,
    page,
    size,
  )
}
</script>

<template>
  <a-modal
    v-model:open="dialogVisible"
    :mask="false"
    centered
    class="h-180"
    :width="1200"
    :footer="null"
  >
    <div class="w-full !h-full">
      <div class="h-5" />
      <div class="font-semibold text-lg text-slate-600">搜索选择设备</div>
      <div class="h-2" />
      <a-card class="w-full" :bordered="false">
        <div class="flex">
          <div class="w-40 flex flex-col items-start">
            <span class="!text-sm">设备名称</span>
            <div class="h-2" />
            <a-input
              class=""
              v-model:value="searchDeviceName"
              placeholder="请输入"
              @pressEnter="handleSearchDevices"
            />
          </div>

          <div class="w-5" />
          <div class="w-40 flex flex-col items-start">
            <span class="!text-sm">设备码</span>
            <div class="h-2" />
            <a-input
              class=""
              v-model:value="searchDeviceCode"
              placeholder="请输入"
              @pressEnter="handleSearchDevices"
            />
          </div>

          <div class="w-5" />
          <div class="w-40 flex flex-col items-start">
            <span class="!text-sm">IP地址</span>
            <div class="h-2" />
            <a-input
              class=""
              v-model:value="searchDeviceIp"
              placeholder="请输入"
              @pressEnter="handleSearchDevices"
            />
          </div>

          <div class="w-5" />
          <div class="w-40 flex flex-col items-start">
            <span class="!text-sm">设备状态</span>
            <div class="h-2" />
            <a-select
              v-model:value="searchOnlineState"
              placeholder="请选择"
              class="w-full"
              allowClear
            >
              <a-select-option
                v-for="item in searchOnlineOptions"
                :key="item.value"
                :value="item.value"
              >
                {{ item.label }}
              </a-select-option>
            </a-select>
          </div>

          <div class="w-5" />
          <div class="w-20 flex flex-col items-start">
            <span class="!h-5"></span>
            <div class="h-2" />
            <a-button class="w-20" type="primary" @click="handleSearchDevices">搜索</a-button>
          </div>

          <div class="w-5" />
          <div class="w-20 flex flex-col items-start">
            <span class="!h-5"></span>
            <div class="h-2" />
            <a-button class="w-20" type="default" @click="handleClearDevices">清除结果</a-button>
          </div>
        </div>
      </a-card>
    </div>

    <div class="h-5" />

    <div>
      <a-table :data-source="devices" style="width: 100%" row-key="device_id">
        <a-table-column title="设备ID" :min-width="80">
          <template #default="{ record }">
            <span class="!font-bold">{{ record.device_id }}</span>
          </template>
        </a-table-column>

        <a-table-column title="设备名称" :min-width="80">
          <template #default="{ record }">
            <span class="">{{ record.device_name }}</span>
          </template>
        </a-table-column>

        <a-table-column title="IP地址" :min-width="80">
          <template #default="{ record }">
            <span class="">{{ record.device_ip_addr }}</span>
          </template>
        </a-table-column>

        <a-table-column title="使用时长" :min-width="90">
          <template #default="{ record }">
            <span>{{ record.used_time }}</span>
          </template>
        </a-table-column>

        <a-table-column title="网络状态" :min-width="60">
          <template #default="{ record }">
            <a-tag :color="record.online ? 'success' : 'error'">
              {{ record.online ? '在线' : '离线' }}
            </a-tag>
          </template>
        </a-table-column>

        <a-table-column title="是否启用" :min-width="60">
          <template #default="{ record }">
            <a-tag :color="record.active ? 'success' : 'error'">
              {{ record.active ? '已启用' : '已禁用' }}
            </a-tag>
          </template>
        </a-table-column>

        <a-table-column title="创建时间" :min-width="100">
          <template #default="{ record }">
            <span class="!text-small">{{ formatTimestamp(record.created_timestamp) }}</span>
          </template>
        </a-table-column>

        <a-table-column title="操作" :min-width="80">
          <template #default="{ record, index }">
            <a-button
              type="primary"
              size="small"
              @click="handleSelectDevice(index, record)"
            >
              查看设备
            </a-button>
          </template>
        </a-table-column>
      </a-table>
    </div>

    <div class="h-5" />

    <div class="flex justify-center">
      <a-pagination
        v-model:current="currentPage"
        v-model:page-size="pageSize"
        :page-size-options="[20, 40, 60, 80]"
        :size="size"
        :disabled="disabled"
        show-size-changer
        :total="devices.length"
        @change="handlePageChange"
      />
    </div>

    <div class="h-10" />

    <!--    <template #footer>-->
    <!--      <div class="dialog-footer">-->
    <!--        <a-button @click="handleCancel">取消</a-button>-->
    <!--        <a-button type="primary" @click="handleConfirm">确定</a-button>-->
    <!--      </div>-->
    <!--    </template>-->
  </a-modal>
</template>

<style scoped></style>
