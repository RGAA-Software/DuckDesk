import axios from 'axios'
import router from "@/router";
import {ElMessage} from "element-plus";

const http = axios.create({
	baseURL: '/api/v1',
	timeout: 10000
})

// 请求拦截器
http.interceptors.request.use(
	(config) => {
		const token = sessionStorage.getItem('login_token')
		if (token) {
			config.headers.Authorization = token
		}
		return config
	},
	(error) => Promise.reject(error)
)

// 811: 无效登录token

// 响应拦截器：统一处理登录失效
http.interceptors.response.use(
	(response) => {
		// ...
		return response
	},
	async (error) => {
		if (error.response?.status === 811) {
			await handleLoginExpired()
		}
		return Promise.reject(error)
	}
)

// 登录失效处理
const handleLoginExpired = async () => {
	sessionStorage.removeItem('login_token')
	ElMessage.error('登录已失效，请重新登录')
	await router.push('/')
}

export default http
