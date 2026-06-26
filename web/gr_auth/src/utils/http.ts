import axios from 'axios'
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

const LOGIN_EXPIRED_CODES = new Set([811, 812])

// 响应拦截器：统一处理登录失效
http.interceptors.response.use(
	(response) => {
		if (LOGIN_EXPIRED_CODES.has(response.data?.code)) {
			void handleLoginExpired()
		}
		return response
	},
	async (error) => {
		const status = error.response?.status
		const code = error.response?.data?.code
		if (status === 401 || LOGIN_EXPIRED_CODES.has(code)) {
			handleLoginExpired()
		} else if (status === 403) {
			ElMessage.error('没有权限执行该操作')
		}
		return Promise.reject(error)
	}
)

// 登录失效处理
const handleLoginExpired = () => {
	sessionStorage.removeItem('login_token')
	ElMessage.error('登录已失效，请重新登录')
	if (window.location.pathname !== '/') {
		window.location.href = '/'
	}
}

export default http
