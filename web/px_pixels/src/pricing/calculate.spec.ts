import { describe, expect, it } from 'vitest'

import { calculatePricing, normalizeQuantity } from './calculate'
import type { PricingSelection } from './types'

function selection(
    overrides: Partial<PricingSelection> = {},
): PricingSelection {
    return {
        usage: 'internal',
        licenseTerm: 'annual',
        currency: 'CNY',
        quantities: { remote: 5, gaming: 0, rendering: 0 },
        delivery: 'self',
        branding: 'pixels',
        ...overrides,
    }
}

describe('pricing calculator', () => {
    it('normalizes positive quantities to the five-stream minimum', () => {
        expect(normalizeQuantity(0, 5)).toBe(0)
        expect(normalizeQuantity(1, 5)).toBe(5)
        expect(normalizeQuantity(4.9, 5)).toBe(5)
        expect(normalizeQuantity(12.8, 5)).toBe(12)
    })

    it('calculates annual list price without volume discounts', () => {
        const estimate = calculatePricing(
            selection({
                quantities: { remote: 200, gaming: 0, rendering: 0 },
            }),
        )

        expect(estimate.licenseAmount).toBe(25_600_000)
        expect(estimate.firstYearAmount).toBe(25_600_000)
        expect(estimate.secondYearAmount).toBe(25_600_000)
    })

    it('calculates products independently and adds standard delivery', () => {
        const estimate = calculatePricing(
            selection({
                quantities: { remote: 5, gaming: 5, rendering: 5 },
                delivery: 'remote',
            }),
        )

        expect(estimate.licenseAmount).toBe(2_970_000)
        expect(estimate.deliveryAmount).toBe(980_000)
        expect(estimate.firstYearAmount).toBe(3_950_000)
    })

    it('calculates perpetual maintenance with the minimum', () => {
        const estimate = calculatePricing(
            selection({
                licenseTerm: 'perpetual',
                quantities: { remote: 5, gaming: 0, rendering: 0 },
            }),
        )

        expect(estimate.licenseAmount).toBe(2_240_000)
        expect(estimate.coreMaintenanceAmount).toBe(980_000)
        expect(estimate.firstYearAmount).toBe(2_240_000)
        expect(estimate.secondYearAmount).toBe(980_000)
    })

    it('uses twenty percent maintenance above the minimum', () => {
        const estimate = calculatePricing(
            selection({
                licenseTerm: 'perpetual',
                quantities: { remote: 20, gaming: 0, rendering: 0 },
            }),
        )

        expect(estimate.coreMaintenanceAmount).toBe(1_792_000)
    })

    it('applies OEM annual minimum and setup pricing', () => {
        const estimate = calculatePricing(
            selection({ usage: 'oem', licenseTerm: 'perpetual' }),
        )

        expect(estimate.recurringLicenseAmount).toBe(10_000_000)
        expect(estimate.productLines[0]?.unitPrice).toBe(192_000)
        expect(estimate.oemMinimumAdjustmentAmount).toBe(9_040_000)
        expect(estimate.brandingAmount).toBe(19_800_000)
        expect(estimate.firstYearAmount).toBe(29_800_000)
        expect(estimate.secondYearAmount).toBe(12_970_000)
    })

    it('keeps USD pricing isolated from CNY pricing', () => {
        const estimate = calculatePricing(selection({ currency: 'USD' }))

        expect(estimate.licenseAmount).toBe(89_500)
        expect(estimate.currency).toBe('USD')
    })
})
