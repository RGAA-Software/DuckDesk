<script setup lang="ts">
import { onMounted, reactive, ref } from 'vue'
import { Modal } from 'ant-design-vue'
import { createGroup, deleteGroup, groupIds, listAllAdminUsers, listGroups, patchGroup, replaceGroupIds, type GroupView, type UserAdminView } from '@/model/identity_api'

const groups = ref<GroupView[]>([]), users = ref<UserAdminView[]>([])
const open = ref(false), editing = ref<GroupView>()
const form = reactive({ name: '', remark: '', members: [] as string[] })
async function refresh() { const [g,u] = await Promise.all([listGroups(), listAllAdminUsers()]); groups.value=g; users.value=u }
function create() { editing.value=undefined; Object.assign(form,{name:'',remark:'',members:[]}); open.value=true }
async function edit(group: GroupView) { editing.value=group; const members=await groupIds('members',group.gid); Object.assign(form,{name:group.name,remark:group.remark,members}); open.value=true }
async function save() { let group = editing.value ? await patchGroup(editing.value,form.name,form.remark) : await createGroup(form.name,form.remark); group=await replaceGroupIds('members',group,form.members); open.value=false; await refresh() }
function remove(group: GroupView) { Modal.confirm({title:`删除用户组 ${group.name}？`,okType:'danger',async onOk(){await deleteGroup(group);await refresh()}}) }
onMounted(refresh)
</script>
<template>
  <a-space direction="vertical" class="w-full" size="large">
    <a-card title="用户组与成员">
      <template #extra><a-button type="primary" @click="create">新建用户组</a-button></template>
      <a-table :data-source="groups" row-key="gid" :pagination="false">
        <a-table-column title="名称" data-index="name"/><a-table-column title="备注" data-index="remark"/>
        <a-table-column title="成员" data-index="member_count"/><a-table-column title="专属应用" data-index="app_count"/>
        <a-table-column title="操作"><template #default="{record}"><a-space><a-button @click="edit(record)">编辑授权</a-button><a-button danger @click="remove(record)">删除</a-button></a-space></template></a-table-column>
      </a-table>
    </a-card>
  </a-space>
  <a-modal v-model:open="open" :title="editing?'编辑用户组':'新建用户组'" width="680px" @ok="save">
    <a-form layout="vertical"><a-form-item label="名称"><a-input v-model:value="form.name"/></a-form-item><a-form-item label="备注"><a-textarea v-model:value="form.remark"/></a-form-item>
      <a-form-item label="成员"><a-select v-model:value="form.members" mode="multiple" show-search option-filter-prop="label" :max-tag-count="6" :options="users.map(u=>({label:u.username,value:u.uid}))"/></a-form-item>
    </a-form>
  </a-modal>
</template>
