<template>
  <el-table :data="tableData" stripe style="width: 100%">
    <el-table-column label="用户名" prop="name" />
    <el-table-column label="权限" prop="permission" />
  </el-table>

</template>

<script lang="ts" setup>
import { onMounted, ref } from 'vue'
import { ElMessage } from 'element-plus'
import http from '@/utils/http'

interface Author {
  name: string,
  permission: string
}

const tableData = ref<Author[]>([])

// GET 请求函数
const fetchUsers = async () => {
  try {
    const response = await http.get('/query/authors')
    tableData.value = response.data.data || []
  } catch (err: any) {
    tableData.value = []
    ElMessage.error(err.response?.data?.message || '查询管理员列表失败')
  }
}

onMounted(fetchUsers)


</script>
