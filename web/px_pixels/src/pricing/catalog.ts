import type { PriceCatalog } from './types'

export const priceCatalog: PriceCatalog = {
    version: '2026.09-public-4',
    effectiveFrom: '2026-09-20',
    currencies: {
        CNY: {
            currency: 'CNY',
            locale: 'zh-CN',
            minimumStreams: 1,
            products: {
                gaming: 2680_00,
                rendering: 1980_00,
            },
            delivery: {
                remote: 9800_00,
            },
            branding: {
                basic: 29800_00,
                full: 69800_00,
                custom: 98000_00,
            },
            oem: {
                setup: 198000_00,
                annualMinimum: 100000_00,
                streamMultiplier: 1.5,
            },
            maintenance: {
                brandingRate: 0.15,
                brandingMinimum: 8000_00,
                oemRate: 0.15,
            },
        },
        USD: {
            currency: 'USD',
            locale: 'en-US',
            minimumStreams: 1,
            products: {
                gaming: 379_00,
                rendering: 279_00,
            },
            delivery: {
                remote: 1390_00,
            },
            branding: {
                basic: 4200_00,
                full: 9800_00,
                custom: 13800_00,
            },
            oem: {
                setup: 27800_00,
                annualMinimum: 14000_00,
                streamMultiplier: 1.5,
            },
            maintenance: {
                brandingRate: 0.15,
                brandingMinimum: 1120_00,
                oemRate: 0.15,
            },
        },
    },
}
