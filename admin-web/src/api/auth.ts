import request from './index'

interface LoginResponse {
  code: number
  data: {
    token: string
    user: { id: number; username: string; role: string }
  }
}

export async function login(username: string, password: string) {
  const res = await request.post<any, LoginResponse>('/auth/login', {
    username,
    password
  })
  return res.data
}

export async function logout() {
  return request.post('/auth/logout')
}

export async function getMe() {
  return request.get('/auth/me')
}
