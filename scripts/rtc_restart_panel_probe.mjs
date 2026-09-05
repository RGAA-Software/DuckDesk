// Controlled localhost Panel endpoint used by native RTC restart acceptance.
// The connection ticket is read from a short-lived JSON file and is never
// printed. This probe only binds loopback and must not be used as a service.
import { createRequire } from 'node:module'
import fs from 'node:fs'
import https from 'node:https'
import path from 'node:path'

const require = createRequire(import.meta.url)
const protobuf = require('../web/px_web_client/node_modules/protobufjs')
const { WebSocketServer } = require('../web/px_console/node_modules/ws')

function argument(name) {
  const prefix = `--${name}=`
  const value = process.argv.slice(2).find((item) => item.startsWith(prefix))
  return value ? value.slice(prefix.length) : ''
}

const configPath = argument('config')
if (!configPath) {
  throw new Error('missing --config=<path>')
}

// Windows PowerShell 5 writes UTF-8 text with a BOM for `Set-Content -Encoding
// utf8`.  Acceptance runs may use that host, so normalize only the leading BOM
// before parsing the short-lived local configuration.
const configText = fs.readFileSync(configPath, 'utf8').replace(/^\uFEFF/, '')
const config = JSON.parse(configText)
const protoPath = path.join(import.meta.dirname, '../src/px_deps/px_message/px_client_panel_message.proto')
const root = protobuf.parse(fs.readFileSync(protoPath, 'utf8')).root
const CpMessage = root.lookupType('pxcp.CpMessage')

const port = Number(config.port)
const revision = Number(config.revision)
const sendDelayMs = Number(config.send_delay_ms ?? 7000)
const guardDelayMs = Number(config.guard_delay_ms ?? 2500)
let restartSent = false

function encodeMessage(value) {
  const error = CpMessage.verify(value)
  if (error) {
    throw new Error(error)
  }
  return Buffer.from(CpMessage.encode(CpMessage.create(value)).finish())
}

function renewTicket() {
  const base = new URL(String(config.console_base))
  const payload = JSON.stringify({
    renewal_token: String(config.renewal_token),
    client_nonce: String(config.client_nonce),
  })
  const options = {
    hostname: base.hostname,
    port: base.port || 443,
    path: '/api/v1/connection-tickets/renew',
    method: 'POST',
    rejectUnauthorized: false,
    headers: {
      'Content-Type': 'application/json',
      'Content-Length': Buffer.byteLength(payload),
    },
  }
  return new Promise((resolve, reject) => {
    const request = https.request(options, (response) => {
      let body = ''
      response.setEncoding('utf8')
      response.on('data', (chunk) => { body += chunk })
      response.on('end', () => {
        if (response.statusCode !== 200) {
          reject(new Error(`ticket renewal returned HTTP ${response.statusCode}`))
          return
        }
        try {
          const data = JSON.parse(body).data
          if (!data?.ticket || !data?.renewal_token || !data?.stream_id || !data?.rtc_ice_config) {
            reject(new Error('ticket renewal response is incomplete'))
            return
          }
          if (data.stream_id !== config.stream_id) {
            reject(new Error('ticket renewal changed stream identity'))
            return
          }
          config.renewal_token = data.renewal_token
          resolve(data)
        } catch (error) {
          reject(error)
        }
      })
    })
    request.on('error', reject)
    request.write(payload)
    request.end()
  })
}

function sendRestart(socket, requestedRevision, ticket) {
  socket.send(encodeMessage({
    type: 40,
    streamId: config.stream_id,
    rtcIceRestart: {
      connectionTicket: ticket.ticket,
      clientNonce: config.client_nonce,
      instanceId: config.instance_id ?? '',
      iceConfigJson: JSON.stringify(ticket.rtc_ice_config),
      revision: requestedRevision,
    },
  }))
}

const server = new WebSocketServer({ host: '127.0.0.1', port })

server.on('listening', () => {
  console.log(`PANEL_PROBE_READY port=${port}`)
})

server.on('connection', (socket) => {
  console.log('PANEL_PROBE_CONNECTED')
  socket.on('message', (data) => {
    const message = CpMessage.decode(new Uint8Array(data))
    const type = Number(message.type)
    if (message.streamId !== config.stream_id) {
      console.error('PANEL_PROBE_STREAM_MISMATCH')
      socket.close(1008, 'stream mismatch')
      return
    }
    if (type === 0 && !restartSent) {
      restartSent = true
      console.log('PANEL_PROBE_HELLO')
      setTimeout(async () => {
        try {
          if (socket.readyState !== 1) return
          const ticket = await renewTicket()
          if (socket.readyState !== 1) return
          sendRestart(socket, revision, ticket)
          console.log(`PANEL_PROBE_RESTART_SENT revision=${revision}`)
          setTimeout(() => {
            try {
              if (socket.readyState !== 1) return
              sendRestart(socket, revision, ticket)
              sendRestart(socket, revision - 1, ticket)
              console.log(`PANEL_PROBE_GUARDS_SENT revision=${revision}`)
            } catch (error) {
              console.error(`PANEL_PROBE_SEND_ERROR ${error.message}`)
              process.exitCode = 1
            }
          }, guardDelayMs)
        } catch (error) {
          console.error(`PANEL_PROBE_SEND_ERROR ${error.message}`)
          process.exitCode = 1
        }
      }, sendDelayMs)
    } else if (type === 1) {
      socket.send(encodeMessage({
        type: 2,
        streamId: config.stream_id,
        onHeartbeat: {},
      }))
    } else if (type === 41) {
      console.log('PANEL_PROBE_RESTART_REQUEST_RECEIVED')
    }
  })
})

server.on('error', (error) => {
  console.error(`PANEL_PROBE_ERROR ${error.message}`)
  process.exitCode = 1
})

function shutdown() {
  server.close(() => process.exit(process.exitCode ?? 0))
  setTimeout(() => process.exit(process.exitCode ?? 0), 1000).unref()
}

process.on('SIGINT', shutdown)
process.on('SIGTERM', shutdown)
