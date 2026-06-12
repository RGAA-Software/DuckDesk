<template>
  <el-table :data="tableData" stripe style="width: 100%">
    <el-table-column label="Auth ID" prop="auth_id" />
    <el-table-column label="名称" prop="auth_name" />
    <el-table-column label="Machine Code" prop="machine_code" />
    <el-table-column label="创建时间" prop="created_timestamp_ms"/>
    <el-table-column label="结束时间" prop="end_timestamp_ms" /> />
    <!--    <el-table-column label="Appkey" prop="appkey" />-->
    <!--    <el-table-column label="App Secret" prop="app_secret" />-->
    <!--    <el-table-column label="用户名" prop="username" />-->
    <!--    <el-table-column label="密码" prop="password" />-->
    <el-table-column label="授权时间(天)" prop="days" />
    <el-table-column label="剩余时间(天)" prop="left_days" />
    <el-table-column align="left">
      <template #header>
        <el-input v-model="search" size="default" placeholder="搜索"/>
      </template>
      <template #default="scope">
        <el-button size="small" @click="">
          显示
        </el-button>

        <el-button size="small" @click="">
          修改
        </el-button>
        <el-button
          size="small"
          type="danger"
          @click=""
        >
          删除
        </el-button>
      </template>
    </el-table-column>
  </el-table>

</template>

<script lang="ts" setup>
import { computed, ref } from 'vue'
import http from '@/utils/http'

interface Author {
  name: string,
  password: string,
  permission: string
}

const search = ref('')
const tableData = ref<Author[]>([])
const selectedData = ref<Author>()
const dialogVisible = ref(false)


// GET 请求函数
const fetchUsers = async () => {
  // loading.value = true
  // error.value = null
  try {
    const params = {
      author_name: "Visitor",
      author_token: "40e1ef4497dfcaee9b335c961df175fb70f9ff32eb286b273dada11ded8d0317",
      page: 1,
      page_size: 10,
    }
    const response = await http.get('/query/authorizations', {params})
    console.log("the resp: ", response.data)
    tableData.value = response.data.data
    tableData.value.forEach((item) => {
      //item.left_days = Math.floor(item.days - (Date.now() - item.created_timestamp_ms)/24/3600/1000);
    })

    console.log("ok", response.data.data)
  } catch (err: any) {
    console.log("err", err)
    tableData.value = err.message || '请求出错'
  } finally {
    // loading.value = false
  }
}

fetchUsers();


</script>

