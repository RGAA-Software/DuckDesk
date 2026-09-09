import { describe, expect, it } from 'vitest'
import { TextInputWorkflow, validSubmissionText } from '../src/rtc/text_input_workflow'

const target = { instanceId: 'app-a', leaseGeneration: 'lease-a', targetGeneration: '1', inputGeneration: '7', maxBytes: 16384 }
function editing() {
  const model = new TextInputWorkflow()
  model.bind(target)
  model.open()
  model.edit('中文😀')
  return model
}

describe('local text workflow', () => {
  it('blocks composition and double submission without trimming text', () => {
    const model = editing()
    model.edit(' 中文\r\n\t')
    model.setComposing(true)
    expect(model.begin('one')).toBeNull()
    model.setComposing(false)
    expect(model.begin('one')?.text).toBe(' 中文\r\n\t')
    expect(model.begin('two')).toBeNull()
    expect(model.suppressOrdinaryInput).toBe(true)
  })

  it('does not confuse queue acceptance with submission', () => {
    const model = editing()
    model.begin('one')
    model.resolve('one', 'lease-a', 'accepted')
    expect(model.draft).toBe('中文😀')
    expect(model.canSend).toBe(false)
    model.resolve('one', 'lease-a', 'submitted')
    expect(model.draft).toBe('')
  })

  it('preserves a newer draft even when edited back to the same text', () => {
    const model = editing()
    model.begin('one')
    model.edit('new')
    model.edit('中文😀')
    model.resolve('one', 'lease-a', 'submitted')
    expect(model.draft).toBe('中文😀')
    expect(model.begin('one')).toBeNull()
  })

  it('retains draft, reports uncertainty, and requires reopening after reconnect', () => {
    const model = editing()
    model.begin('one')
    model.disconnect()
    expect(model.outcome).toBe('outcome_unknown')
    expect(model.canSend).toBe(false)
    model.bind({ ...target, leaseGeneration: 'lease-b' })
    expect(model.canSend).toBe(false)
    expect(model.resolve('one', 'lease-a', 'submitted')).toBe(false)
    expect(model.draft).toBe('中文😀')
    model.open()
    expect(model.begin('two')).not.toBeNull()
  })

  it('rejects stale or mismatched replies and clears private data on revocation', () => {
    const model = editing()
    model.begin('one')
    expect(model.resolve('other', 'lease-a', 'submitted')).toBe(false)
    expect(model.resolve('one', 'old-lease', 'submitted')).toBe(false)
    model.clear()
    expect(model.draft).toBe('')
    expect(model.resolve('one', 'lease-a', 'submitted')).toBe(false)
    expect(model.open()).toBe(false)
  })

  it('isolates instances and repeatedly stops safely', () => {
    const model = editing()
    model.bind({ ...target, instanceId: 'app-b' })
    expect(model.draft).toBe('')
    model.clear()
    model.clear()
    expect(model.suppressOrdinaryInput).toBe(false)
  })

  it('keeps input suppressed if the panel closes while a submission is pending', () => {
    const model = editing()
    model.begin('one')
    model.close()
    expect(model.suppressOrdinaryInput).toBe(true)
    model.resolve('one', 'lease-a', 'failed')
    expect(model.suppressOrdinaryInput).toBe(false)
    expect(model.draft).toBe('中文😀')
  })
})

describe('text validation', () => {
  it('counts UTF-8 bytes and preserves whitespace', () => {
    expect(validSubmissionText('中', 3)).toBe(true)
    expect(validSubmissionText('中', 2)).toBe(false)
    expect(validSubmissionText('😀', 4)).toBe(true)
    expect(validSubmissionText(' \t\r\n', 4)).toBe(true)
    expect(validSubmissionText('a'.repeat(16384), 16384)).toBe(true)
    expect(validSubmissionText('a'.repeat(16385), 16384)).toBe(false)
  })

  it.each(['', '\0', '\x01', '\x7f', '\ud800', '\udc00', '\ud800x'])('rejects invalid text %j', text => {
    expect(validSubmissionText(text, 16384)).toBe(false)
  })
})
