export const MAX_AUTH_NAME_LEN = 128
export const MAX_MACHINE_CODE_LEN = 256
export const MAX_AUTH_DAYS = 365000
export const MAX_AUTH_STREAMS = 10000

const VALID_CUSTOMER_ROLES = new Set([1, 2, 3])

export interface CreateAuthorizationForm {
  name: string
  machine_code: string
  role: string | number
  days: string | number
  max_streams: string | number
}

export interface UpdateAuthorizationForm {
  days: string | number
  max_streams: string | number
  role: string | number
}

export interface CreateAuthorizationPayload {
  name: string
  machine_code: string
  role: number
  days: number
  max_streams: number
}

export interface UpdateAuthorizationPayload {
  role: number
  days: number
  max_streams: number
}

type ValidationResult<T> =
  | { ok: true; value: T }
  | { ok: false; message: string }

const toInteger = (value: string | number) => {
  if (value === '') return Number.NaN
  const parsed = Number(value)
  return Number.isInteger(parsed) ? parsed : Number.NaN
}

const validateDays = (days: number): ValidationResult<number> => {
  if (!Number.isInteger(days) || days < 1 || days > MAX_AUTH_DAYS) {
    return { ok: false, message: `Days 必须在 1 到 ${MAX_AUTH_DAYS} 之间` }
  }
  return { ok: true, value: days }
}

const validateMaxStreams = (maxStreams: number): ValidationResult<number> => {
  if (!Number.isInteger(maxStreams) || maxStreams < 1 || maxStreams > MAX_AUTH_STREAMS) {
    return { ok: false, message: `Max Streams 必须在 1 到 ${MAX_AUTH_STREAMS} 之间` }
  }
  return { ok: true, value: maxStreams }
}

const validateRole = (role: number): ValidationResult<number> => {
  if (!VALID_CUSTOMER_ROLES.has(role)) {
    return { ok: false, message: 'Customer Role 必须是 1、2 或 3' }
  }
  return { ok: true, value: role }
}

export const validateCreateAuthorization = (
  form: CreateAuthorizationForm,
): ValidationResult<CreateAuthorizationPayload> => {
  const name = form.name.trim()
  const machineCode = form.machine_code.trim()
  const role = toInteger(form.role)
  const days = toInteger(form.days)
  const maxStreams = toInteger(form.max_streams)

  if (!name || !machineCode || form.role === '' || form.days === '' || form.max_streams === '') {
    return { ok: false, message: '请填写完整授权信息' }
  }
  if (name.length > MAX_AUTH_NAME_LEN) {
    return { ok: false, message: `User Name 不能超过 ${MAX_AUTH_NAME_LEN} 个字符` }
  }
  if (machineCode.length > MAX_MACHINE_CODE_LEN) {
    return { ok: false, message: `Machine Code 不能超过 ${MAX_MACHINE_CODE_LEN} 个字符` }
  }

  const daysResult = validateDays(days)
  if (!daysResult.ok) return daysResult

  const maxStreamsResult = validateMaxStreams(maxStreams)
  if (!maxStreamsResult.ok) return maxStreamsResult

  const roleResult = validateRole(role)
  if (!roleResult.ok) return roleResult

  return {
    ok: true,
    value: {
      name,
      machine_code: machineCode,
      role,
      days,
      max_streams: maxStreams,
    },
  }
}

export const validateUpdateAuthorization = (
  form: UpdateAuthorizationForm,
): ValidationResult<UpdateAuthorizationPayload> => {
  const days = toInteger(form.days)
  const maxStreams = toInteger(form.max_streams)
  const role = toInteger(form.role)

  const daysResult = validateDays(days)
  if (!daysResult.ok) return daysResult

  const maxStreamsResult = validateMaxStreams(maxStreams)
  if (!maxStreamsResult.ok) return maxStreamsResult

  const roleResult = validateRole(role)
  if (!roleResult.ok) return roleResult

  return {
    ok: true,
    value: {
      days,
      max_streams: maxStreams,
      role,
    },
  }
}
