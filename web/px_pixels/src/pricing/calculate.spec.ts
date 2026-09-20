import { describe, expect, it } from 'vitest'

import { calculatePricing, normalizeQuantity } from './calculate'
import type { PricingSelection } from './types'

function selection(
    overrides: Partial<PricingSelection> = {},
): PricingSelection {
    return {
        usage: 'internal',
        currency: 'CNY',
        quantities: { gaming: 1, rendering: 1 },
        delivery: 'self',
        branding: 'pixels',
        ...overrides,
    }
}

describe('pricing calculator', () => {
    it('allows zero and normalizes selected products from one stream', () => {
        expect(normalizeQuantity(0, 1)).toBe(0)
        expect(normalizeQuantity(0.9, 1)).toBe(1)
        expect(normalizeQuantity(1, 1)).toBe(1)
        expect(normalizeQuantity(12.8, 1)).toBe(12)
    })

    it('calculates annual list price without volume discounts', () => {
        const estimate = calculatePricing(
            selection({
                quantities: { gaming: 200, rendering: 0 },
            }),
        )

        expect(estimate.licenseAmount).toBe(53_600_000)
        expect(estimate.firstYearAmount).toBe(53_600_000)
        expect(estimate.secondYearAmount).toBe(53_600_000)
    })

    it('calculates products independently and adds standard delivery', () => {
        const estimate = calculatePricing(
            selection({
                quantities: { gaming: 5, rendering: 5 },
                delivery: 'remote',
            }),
        )

        expect(estimate.licenseAmount).toBe(2_330_000)
        expect(estimate.deliveryAmount).toBe(980_000)
        expect(estimate.firstYearAmount).toBe(3_310_000)
    })

    it('applies OEM annual minimum and setup pricing', () => {
        const estimate = calculatePricing(selection({ usage: 'oem' }))

        expect(estimate.recurringLicenseAmount).toBe(10_000_000)
        expect(estimate.productLines[0]?.unitPrice).toBe(402_000)
        expect(estimate.oemMinimumAdjustmentAmount).toBe(9_301_000)
        expect(estimate.brandingAmount).toBe(19_800_000)
        expect(estimate.firstYearAmount).toBe(29_800_000)
        expect(estimate.secondYearAmount).toBe(12_970_000)
    })

    it('keeps USD pricing isolated from CNY pricing', () => {
        const estimate = calculatePricing(selection({ currency: 'USD' }))

        expect(estimate.licenseAmount).toBe(65_800)
        expect(estimate.currency).toBe('USD')
    })
})
