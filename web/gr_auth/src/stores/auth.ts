import { defineStore } from 'pinia'

export const useAuthStore = defineStore('auth', {
	state: () => ({
		refreshFlag: 0
	}),
	actions: {
		triggerRefresh() {
			this.refreshFlag++
		}
	}
})
