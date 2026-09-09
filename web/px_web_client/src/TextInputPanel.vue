<script setup lang="ts">
import { computed, nextTick, onBeforeUnmount, onMounted, ref } from 'vue'
import type { TextInputWorkflow, TextSubmission } from './rtc/text_input_workflow'

// The composition owner must pass a reactive workflow, acknowledge the remote
// input barrier before opening, and place this panel inside its fullscreen root.
const props = defineProps<{ workflow: TextInputWorkflow }>()
const emit = defineEmits<{
  submit: [submission: TextSubmission]
  close: []
}>()
const editor = ref<HTMLTextAreaElement | null>(null)
const viewportHeight = ref('100dvh')
let compositionSettling = false
let alive = true
const status = computed(() => {
  const outcome = props.workflow.outcome
  if (outcome === 'submitted') return '文字已交给目标应用；请查看远端结果。'
  if (outcome === 'accepted') return '请求已接收，等待执行结果。'
  if (outcome === 'outcome_unknown') return '连接中断或结果不确定，请先查看远端，不要重复发送。'
  if (outcome) return `未完成文字提交：${outcome}。草稿已保留。`
  return '发送到远端当前有效的输入目标，不会额外发送 Enter。'
})

function updateViewport() {
  viewportHeight.value = `${window.visualViewport?.height ?? window.innerHeight}px`
}

function input(event: Event) {
  if (event.target instanceof HTMLTextAreaElement) props.workflow.edit(event.target.value)
}

function compositionStart() {
  compositionSettling = false
  props.workflow.setComposing(true)
}

async function compositionEnd() {
  compositionSettling = true
  // The browser may deliver its final input after compositionend. Keep sending
  // disabled through Vue's flush; the button never submits on Enter/keydown.
  await nextTick()
  if (!alive || !compositionSettling) return
  if (editor.value) props.workflow.edit(editor.value.value)
  props.workflow.setComposing(false)
  compositionSettling = false
}

function submit() {
  if (compositionSettling) return
  // getRandomValues also works on the existing intranet HTTP deployment;
  // randomUUID is restricted to secure contexts in several supported browsers.
  const bytes = crypto.getRandomValues(new Uint8Array(16))
  const requestId = Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('')
  const submission = props.workflow.begin(requestId)
  if (submission) emit('submit', submission)
}

function close() {
  props.workflow.close()
  emit('close')
}

// Called synchronously from a user gesture by the owner for mobile keyboards.
function focus() { editor.value?.focus() }
defineExpose({ focus })

onMounted(() => {
  updateViewport()
  window.visualViewport?.addEventListener('resize', updateViewport)
  window.addEventListener('resize', updateViewport)
})
onBeforeUnmount(() => {
  alive = false
  compositionSettling = false
  props.workflow.setComposing(false)
  window.visualViewport?.removeEventListener('resize', updateViewport)
  window.removeEventListener('resize', updateViewport)
})
</script>

<template>
  <section v-show="workflow.isOpen" class="text-input-panel" role="dialog" aria-label="输入文字"
    :style="{ maxHeight: `calc(${viewportHeight} - 24px)` }"
    @keydown.stop @keyup.stop @pointerdown.stop @pointerup.stop @click.stop @wheel.stop @touchstart.stop @touchmove.stop>
    <label class="editor-label">输入文字
      <textarea ref="editor" :value="workflow.draft" rows="5" autocomplete="off" autocapitalize="off" spellcheck="false"
        @input="input" @compositionstart="compositionStart" @compositionend="compositionEnd" />
    </label>
    <p>请使用本机输入法选词，再点击发送。多行文字可能被目标应用解释为提交。</p>
    <p role="status" aria-live="polite">{{ status }}</p>
    <div class="actions">
      <button type="button" @click="close">关闭</button>
      <button type="button" :disabled="!workflow.canSend" @click="submit">发送文字</button>
    </div>
  </section>
</template>

<style scoped>
.text-input-panel {
  position: absolute;
  z-index: 100;
  right: max(12px, env(safe-area-inset-right));
  bottom: max(12px, env(safe-area-inset-bottom));
  box-sizing: border-box;
  width: min(440px, calc(100% - 24px));
  padding: 16px;
  overflow: auto;
  color: #f4f4f5;
  background: #202329;
  border: 1px solid #686b73;
  border-radius: 10px;
  touch-action: auto;
}
.editor-label { display: grid; gap: 8px; }
textarea { width: 100%; box-sizing: border-box; resize: vertical; font: inherit; font-size: 16px; }
p { font-size: 13px; line-height: 1.5; }
.actions { display: flex; justify-content: flex-end; gap: 12px; }
button { min-height: 40px; padding: 6px 14px; cursor: pointer; }
button:disabled { cursor: not-allowed; }
</style>
