import axiosHttp from '@/http.ts'

export async function queryMachineCode() {
  const resp = await axiosHttp.get('/api/v1/cms/control/get/machine/code', {
    params: {},
  })
  if (resp.status !== 200) {
    console.error('get machine code failed', resp)
    return null
  }

  const data = resp.data
  if (data.code !== 200) {
    console.error('ge machine code failed, data:', data)
    return null
  }
  console.log('machine code: ', data.data)
  return data.data
}
