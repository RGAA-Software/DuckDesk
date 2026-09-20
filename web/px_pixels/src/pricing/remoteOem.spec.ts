import { describe, expect, it } from 'vitest'

import {
    calculateRemoteOem,
    normalizeRemoteOemStreams,
} from './remoteOem'

describe('remote desktop OEM calculator', () => {
    it('normalizes concurrency to at least one stream', () => {
        expect(normalizeRemoteOemStreams(0)).toBe(1)
        expect(normalizeRemoteOemStreams(12.9)).toBe(12)
    })

    it('applies the annual minimum below the threshold', () => {
        const estimate = calculateRemoteOem(20, 'CNY')

        expect(estimate.calculatedStreamAmount).toBe(3_840_000)
        expect(estimate.annualLicenseAmount).toBe(10_000_000)
        expect(estimate.minimumAdjustmentAmount).toBe(6_160_000)
        expect(estimate.firstYearAmount).toBe(29_800_000)
    })

    it('uses stream pricing above the annual minimum', () => {
        const estimate = calculateRemoteOem(100, 'CNY')

        expect(estimate.annualLicenseAmount).toBe(19_200_000)
        expect(estimate.minimumAdjustmentAmount).toBe(0)
        expect(estimate.renewalAmount).toBe(22_170_000)
    })
})
