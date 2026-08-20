<script setup lang="ts">
import { onMounted, ref } from 'vue'
import { message } from 'ant-design-vue'
import { getInstances, openInstance, stopInstance, type InstanceView } from './api'
const instances = ref<InstanceView[]>([])
async function refresh() { instances.value = await getInstances() }
async function stop(item: InstanceView) { try { await stopInstance(item.instance_id); await refresh() } catch { message.error('停止失败') } }
onMounted(refresh)
</script>
<template>
  <a-card title="我的实例与活动">
    <template #extra><a-button @click="refresh">刷新</a-button></template>
    <a-table :data-source="instances" row-key="instance_id" :pagination="false">
      <a-table-column title="应用" data-index="app_name" />
      <a-table-column title="状态" data-index="state" />
      <a-table-column title="创建时间"><template #default="{ record }">{{ new Date(record.created_at).toLocaleString() }}</template></a-table-column>
      <a-table-column title="操作"><template #default="{ record }">
        <a-space><a-button v-if="record.reconnectable" @click="openInstance(record)">进入</a-button><a-button danger :disabled="['stopped','failed'].includes(record.state)" @click="stop(record)">停止</a-button></a-space>
      </template></a-table-column>
    </a-table>
  </a-card>
</template>
