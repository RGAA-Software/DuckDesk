<script setup lang="ts">
import { computed, ref } from 'vue'
import { queryDevices } from '@/model/device_api.ts'
import type { Device } from '@/entity/device.ts'
import { formatTimestamp } from '@/util/time.ts'
import { type ComponentSize, ElNotification } from 'element-plus'

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
const size = ref<ComponentSize>('default')
const background = ref(false)
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

const handleClearDevices = async () => {
  searchDeviceName.value = ''
  searchDeviceCode.value = ''
  searchDeviceIp.value = ''
  devices.value = []
}

const handleSelectDevice = async (index: number, device: Device) => {
  console.log('index: ', index, ', device: ', device)

  if (!device.online) {
    ElNotification({
      message: '此设备: ' + device.device_name + ' 已经离线',
      type: 'warning',
    })
    return
  }

  emit('select', device)

  // 顺便关闭 dialog
  emit('update:modelValue', false)
}

const handleSizeChange = async (val: number) => {
  pageSize.value = val
  console.log('current page: ', currentPage.value, ' page size: ', pageSize.value)
  devices.value = await queryDevices('', '', '', '', currentPage.value, pageSize.value)
}

const handleCurrentChange = async (val: number) => {
  currentPage.value = val
  devices.value = await queryDevices('', '', '', '', currentPage.value, pageSize.value)
}
</script>

<template>
  <el-dialog
    v-model="dialogVisible"
    :modal="false"
    modal-penetrable
    align-center
    class="h-180"
    :width="1200"
  >
    <div class="w-full !h-full">
      <div class="h-5" />
      <div class="font-semibold text-lg text-slate-600">搜索选择设备</div>
      <div class="h-2" />
      <el-card class="w-full" shadow="never">
        <div class="flex">
          <div class="w-40 flex flex-col items-start">
            <span class="!text-sm">设备名称</span>
            <div class="h-2" />
            <el-input class="" v-model="searchDeviceName" placeholder="请输入"></el-input>
          </div>

          <div class="w-5" />
          <div class="w-40 flex flex-col items-start">
            <span class="!text-sm">设备码</span>
            <div class="h-2" />
            <el-input class="" v-model="searchDeviceCode" placeholder="请输入"></el-input>
          </div>

          <div class="w-5" />
          <div class="w-40 flex flex-col items-start">
            <span class="!text-sm">IP地址</span>
            <div class="h-2" />
            <el-input class="" v-model="searchDeviceIp" placeholder="请输入"></el-input>
          </div>

          <div class="w-5" />
          <div class="w-40 flex flex-col items-start">
            <span class="!text-sm">设备状态</span>
            <div class="h-2" />
            <el-select v-model="searchOnlineState" placeholder="请选择" class="" clearable>
              <el-option
                v-for="item in searchOnlineOptions"
                :key="item.value"
                :label="item.label"
                :value="item.value"
              />
            </el-select>
          </div>

          <div class="w-5" />
          <div class="w-20 flex flex-col items-start">
            <span class="!h-5"></span>
            <div class="h-2" />
            <el-button class="w-20" type="primary" @click="handleSearchDevices">搜索</el-button>
          </div>

          <div class="w-5" />
          <div class="w-20 flex flex-col items-start">
            <span class="!h-5"></span>
            <div class="h-2" />
            <el-button class="w-20" type="warning" @click="handleClearDevices">清除结果</el-button>
          </div>
        </div>
      </el-card>
    </div>

    <div class="h-5" />

    <div>
      <el-table :data="devices" style="width: 100%">
        <el-table-column label="设备ID" :min-width="80">
          <template #default="scope">
            <span class="!font-bold">{{ scope.row.device_id }}</span>
          </template>
        </el-table-column>

        <el-table-column label="设备名称" :min-width="80">
          <template #default="scope">
            <span class="">{{ scope.row.device_name }}</span>
          </template>
        </el-table-column>

        <el-table-column label="IP地址" :min-width="80">
          <template #default="scope">
            <span class="">{{ scope.row.device_ip_addr }}</span>
          </template>
        </el-table-column>

        <el-table-column label="使用时长" :min-width="90">
          <template #default="scope">
            <span>{{ scope.row.used_time }}</span>
          </template>
        </el-table-column>

        <el-table-column label="网络状态" :min-width="60">
          <template #default="scope">
            <el-tag :type="scope.row.online ? 'success' : 'danger'" effect="light">
              {{ scope.row.online ? '在线' : '离线' }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column label="是否启用" :min-width="60">
          <template #default="scope">
            <el-tag :type="scope.row.active ? 'success' : 'danger'" effect="light">
              {{ scope.row.active ? '已启用' : '已禁用' }}
            </el-tag>
          </template>
        </el-table-column>

        <el-table-column label="创建时间" :min-width="100">
          <template #default="scope">
            <span class="!text-small">{{ formatTimestamp(scope.row.created_timestamp) }}</span>
          </template>
        </el-table-column>

        <el-table-column label="操作" :min-width="80">
          <template #default="scope">
            <el-button
              type="primary"
              size="small"
              @click="handleSelectDevice(scope.$index, scope.row)"
            >
              查看设备
            </el-button>
          </template>
        </el-table-column>
      </el-table>
    </div>

    <div class="h-5" />

    <div class="flex justify-center">
      <el-pagination
        v-model:current-page="currentPage"
        v-model:page-size="pageSize"
        :page-sizes="[20, 40, 60, 80]"
        :size="size"
        :disabled="disabled"
        :background="background"
        layout="total, sizes, prev, pager, next"
        :total="devices.length"
        @size-change="handleSizeChange"
        @current-change="handleCurrentChange"
      />
    </div>

    <div class="h-10" />

    <!--    <template #footer>-->
    <!--      <div class="dialog-footer">-->
    <!--        <el-button @click="handleCancel">取消</el-button>-->
    <!--        <el-button type="primary" @click="handleConfirm">确定</el-button>-->
    <!--      </div>-->
    <!--    </template>-->
  </el-dialog>
</template>

<style scoped></style>
