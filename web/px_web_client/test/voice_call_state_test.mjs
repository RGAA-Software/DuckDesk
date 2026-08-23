import assert from 'node:assert/strict'
import { readFile } from 'node:fs/promises'
import path from 'node:path'
import ts from 'typescript'

const sourcePath = path.join(import.meta.dirname, '../src/rtc/voice_call_state.ts')
const source = await readFile(sourcePath, 'utf8')
const js = ts.transpileModule(source, {
  compilerOptions: { module: ts.ModuleKind.ESNext, target: ts.ScriptTarget.ES2022 },
}).outputText
const state = await import(`data:text/javascript;base64,${Buffer.from(js).toString('base64')}`)

const identity = { callId: 'call-1', requestId: '9007199254740991' }
assert.equal(state.matchesPendingVoiceResponse('outgoing', identity, {
  callId: 'call-1', requestId: { toString: () => '9007199254740991' },
}), true)
assert.equal(state.matchesPendingVoiceResponse('connected', identity, {
  callId: 'call-1', requestId: '9007199254740991',
}), false)
assert.equal(state.matchesPendingVoiceResponse('outgoing', identity, {
  callId: 'forged', requestId: '9007199254740991',
}), false)
assert.equal(state.matchesPendingVoiceResponse('outgoing', identity, {
  callId: 'call-1', requestId: '7',
}), false)
assert.equal(state.matchesActiveVoiceCall(identity, 'call-1'), true)
assert.equal(state.matchesActiveVoiceCall(identity, 'stale-call'), false)
assert.equal(state.isSupportedVoiceAudioConfig(identity, {
  callId: 'call-1', sampleRate: 48_000, channels: 1, frameMs: 20,
}), true)
for (const invalid of [
  { callId: 'stale-call', sampleRate: 48_000, channels: 1, frameMs: 20 },
  { callId: 'call-1', sampleRate: 44_100, channels: 1, frameMs: 20 },
  { callId: 'call-1', sampleRate: 48_000, channels: 2, frameMs: 20 },
  { callId: 'call-1', sampleRate: 48_000, channels: 1, frameMs: 10 },
]) {
  assert.equal(state.isSupportedVoiceAudioConfig(identity, invalid), false)
}

const appSource = await readFile(path.join(import.meta.dirname, '../src/App.vue'), 'utf8')
const zhSource = await readFile(path.join(import.meta.dirname, '../src/locales/zh.ts'), 'utf8')
assert.match(appSource, /voiceCallRequiresHeadset\.value\s*&&\s*!voicePreflightShown/)
assert.match(appSource, /window\.isSecureContext[\s\S]*?navigator\.mediaDevices\?\.getUserMedia/)
assert.match(appSource, /remoteVoiceSupported\s*&&\s*voiceBrowserMediaAvailable/)
assert.match(appSource, /await ElMessageBox\.confirm\(/)
assert.match(appSource, /voicePreflightShown\s*=\s*true/)
assert.match(appSource, /finishVoiceCall[\s\S]*?voicePreflightShown\s*=\s*false/)
assert.match(zhSource, /voicePreflightWarning:.*系统声音.*暂停/s)
assert.match(zhSource, /voicePreflightContinue:.*已暂停远控声音/s)

console.log('voice_call_state: 19 assertions passed')
