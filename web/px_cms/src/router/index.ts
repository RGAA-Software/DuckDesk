import { createRouter, createWebHistory } from 'vue-router'
import HomeView from '../views/HomeView.vue'
import AuthView from '@/views/AuthView.vue'
import ResourcesView from '@/views/ResourcesView.vue'
import DevicesList from '@/views/DevicesList.vue'
import SecurityInternal from '@/views/SecurityInternal.vue'
import UserManager from '@/views/UserManager.vue'
import GroupManager from '@/views/GroupManager.vue'
import ProfileInfo from '@/views/ProfileInfo.vue'
import OnlineConnection from '@/views/OnlineConnection.vue'
import ConnectionMonitor from '@/views/ConnectionMonitor.vue'
import VideoWall from '@/views/VideoWall.vue'
import EventView from '@/views/EventView.vue'
import LoginView from '@/views/LoginView.vue'
import AppsView from '@/views/AppsView.vue'
import DeviceRecords from '@/views/DeviceRecords.vue'
import LiveViewer from '@/views/LiveViewer.vue'
import { queryAdminSession } from '@/model/admin_session_api.ts'
import UserLayout from '@/user/UserLayout.vue'
import UserLogin from '@/user/UserLogin.vue'
import UserHome from '@/user/UserHome.vue'
import UserDevices from '@/user/UserDevices.vue'
import UserApps from '@/user/UserApps.vue'
import UserActivity from '@/user/UserActivity.vue'
import UserProfile from '@/user/UserProfile.vue'
import PublicApps from '@/user/PublicApps.vue'
import { queryUser } from '@/user/api'

const router = createRouter({
  history: createWebHistory(import.meta.env.BASE_URL),
  routes: [
    {
      path: '/home',
      name: 'home',
      component: HomeView,
      redirect: '/resources',
      children: [
        {
          path: '/resources',
          name: 'resources',
          component: ResourcesView,
          meta: {
            title: '资源总览',
            requiresAuth: true,
          },
        },
        {
          path: '/devices-list',
          name: 'devices-list',
          component: DevicesList,
          meta: {
            title: '设备列表',
            requiresAuth: true,
          },
        },
        {
          path: '/online-connection',
          name: 'online-connection',
          component: OnlineConnection,
          meta: {
            title: '在线连接',
            requiresAuth: true,
          },
        },
        {
          path: '/connection-monitor',
          name: 'connection-monitor',
          component: ConnectionMonitor,
          meta: {
            title: '连接监控',
            requiresAuth: true,
          },
        },
        {
          path: '/video-wall',
          name: 'video-wall',
          component: VideoWall,
          meta: {
            title: '设备监控',
            requiresAuth: true,
          },
        },
        {
          path: '/live-viewer',
          name: 'live-viewer',
          component: LiveViewer,
          meta: {
            title: '直播观看',
            requiresAuth: true,
          },
        },
        {
          path: '/apps',
          name: 'apps',
          component: AppsView,
          meta: {
            title: '应用调度',
            requiresAuth: true,
          },
        },
        {
          path: '/records/:device_id',
          name: 'device-records',
          component: DeviceRecords,
          meta: {
            title: '设备录像',
            requiresAuth: true,
          },
        },
        {
          path: '/security-internal',
          name: 'security-internal',
          component: SecurityInternal,
          meta: {
            title: '安全审计',
            requiresAuth: true,
          },
        },
        {
          path: '/events',
          name: 'events',
          component: EventView,
          meta: {
            title: '上报事件',
            requiresAuth: true,
          },
        },
        {
          path: '/user-manager',
          name: 'user-manager',
          component: UserManager,
          meta: {
            title: '用户管理',
            requiresAuth: true,
          },
        },
        {
          path: '/group-manager',
          name: 'group-manager',
          component: GroupManager,
          meta: { title: '用户组管理', requiresAuth: true },
        },
        {
          path: '/profile-info',
          name: 'profile-info',
          component: ProfileInfo,
          meta: {
            title: '个人中心',
            requiresAuth: true,
          },
        },
      ],
    },
    {
      path: '/auth',
      name: 'auth',
      component: AuthView,
    },
    {
      path: '/',
      name: 'login',
      component: LoginView,
      meta: {
        title: '登录',
        requiresAuth: false,
      },
    },
    {
      path: '/user/login',
      name: 'user-login',
      component: UserLogin,
      meta: { title: '用户登录' },
    },
    { path: '/user/public-apps', component: PublicApps, meta: { title: '公共云端应用' } },
    {
      path: '/user',
      component: UserLayout,
      redirect: '/user/home',
      children: [
        { path: 'home', component: UserHome, meta: { title: '我的资源', requiresUser: true } },
        { path: 'devices', component: UserDevices, meta: { title: '我的远程桌面', requiresUser: true } },
        { path: 'apps', component: UserApps, meta: { title: '云端应用', requiresUser: true } },
        { path: 'activity', component: UserActivity, meta: { title: '实例与活动', requiresUser: true } },
        { path: 'profile', component: UserProfile, meta: { title: '个人中心', requiresUser: true } },
      ],
    },
  ],
})

router.beforeEach(async (to) => {
  if (to.meta.requiresUser) {
    const user = await queryUser()
    if (!user) return { path: '/user/login', query: { redirect: to.fullPath }, replace: true }
    if (user.must_change_password && to.path !== '/user/profile') {
      return { path: '/user/profile', replace: true }
    }
    return true
  }
  if (!to.meta.requiresAuth) return true
  const admin = await queryAdminSession()
  return admin ? true : { path: '/', replace: true }
})


export default router
