import { createRouter, createWebHistory } from 'vue-router'
import HomeView from '../views/HomeView.vue'
import AuthView from '@/views/AuthView.vue'
import ResourcesView from '@/views/ResourcesView.vue'
import DevicesList from '@/views/DevicesList.vue'
import SecurityInternal from '@/views/SecurityInternal.vue'
import UserManager from '@/views/UserManager.vue'
import ProfileInfo from '@/views/ProfileInfo.vue'
import OnlineConnection from '@/views/OnlineConnection.vue'
import ConnectionMonitor from '@/views/ConnectionMonitor.vue'
import VideoWall from '@/views/VideoWall.vue'
import EventView from '@/views/EventView.vue'
import LoginView from '@/views/LoginView.vue'
import AppsView from '@/views/AppsView.vue'

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
            title: '多画面墙',
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
  ],
})

router.beforeEach((to, from, next) => {
  const isLoggedIn = !!localStorage.getItem('token')

  if (!isLoggedIn && to.meta.requiresAuth) {
    next({ path: '/', replace: true })
  } else {
    next()
  }
})


export default router
