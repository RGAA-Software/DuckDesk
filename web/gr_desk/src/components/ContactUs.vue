<script setup lang="ts">
import { computed, ref } from 'vue'
import axios from 'axios'
import { ElNotification } from 'element-plus'
import axiosHttp from '@/http.ts'

interface Consult {
  title: string
  yourName: string
  consultType: string
  content: string
  email: string
  wechat: string
  qq: string
}

const consult = ref<Consult>({
  title: '',
  yourName: '',
  consultType: '',
  content: '',
  email: '',
  wechat: '',
  qq: '',
})

// 父组件传入的 v-model
const props = defineProps<{
  modelValue: boolean
}>()

const emit = defineEmits<{
  (e: 'update:modelValue', value: boolean): void
}>()

// 内部代理状态（关键）
const visible = computed({
  get: () => props.modelValue,
  set: (val) => emit('update:modelValue', val),
})

const close = () => {
  visible.value = false
}

async function confirm() {
  // 内部业务逻辑
  visible.value = false

  await postContact()
}

async function postContact() {
  try {
    const { data } = await axiosHttp.post(
      '/api/v1/create/new/consult',
      {
        title: consult.value.title,
        your_name: consult.value.yourName,
        consult_type: consult.value.consultType,
        content: consult.value.content,
        email: consult.value.email,
        wechat: consult.value.wechat,
        qq: consult.value.wechat,
      },
      {
        headers: {
          'Content-Type': 'application/json',
        },
      },
    )
    console.log('data: ', data)

    ElNotification({
      title: '提交成功',
      message: '已收到您的咨询, 我们会尽快回复',
      type: 'primary',
    })
  } catch (error) {
    console.log('create consult error:', error)
    ElNotification({
      title: '提交失败',
      message: '请填写必要信息后再提交',
      type: 'warning',
    })
  }
}
</script>

<template>
  <el-dialog v-model="visible" :modal="false" modal-penetrable align-center>
    <template #header>
      <el-text class="!text-lg !text-slate-700">请填写您的咨询信息</el-text>
    </template>

    <el-form :model="consult" label-width="auto" style="max-width: 600px">
      <el-form-item label="您要咨询的是*">
        <el-input v-model="consult.title" />
      </el-form-item>

      <el-form-item label="怎么称呼您*">
        <el-input v-model="consult.yourName" />
      </el-form-item>

      <el-form-item label="合作类型*">
        <el-select v-model="consult.consultType" placeholder="请选择合作类型">
          <el-option label="个人" value="personal" />
          <el-option label="企业" value="enterprise" />
        </el-select>
      </el-form-item>

      <el-form-item label="详细内容*">
        <el-input
          v-model="consult.content"
          :rows="2"
          type="textarea"
          placeholder="请输入您想要咨询的内容"
        />
      </el-form-item>

      <el-form-item label="邮件*">
        <el-input v-model="consult.email" />
      </el-form-item>

      <el-form-item label="微信">
        <el-input v-model="consult.wechat" />
      </el-form-item>

      <el-form-item label="QQ">
        <el-input v-model="consult.qq" />
      </el-form-item>
    </el-form>

    <template #footer>
      <div class="dialog-footer">
        <el-button @click="close">取消</el-button>
        <el-button type="primary" @click="confirm"> 提交 </el-button>
      </div>
    </template>
  </el-dialog>
</template>

<style scoped></style>
