import type { PriceCatalog } from './types'

export const priceCatalog: PriceCatalog = {
    version: '2026.09-public-1',
    effectiveFrom: '2026-09-19',
    currencies: {
        CNY: {
            currency: 'CNY',
            locale: 'zh-CN',
            minimumStreams: 5,
            products: {
                remote: { annual: 1280_00, perpetual: 4480_00 },
                gaming: { annual: 2680_00, perpetual: 9380_00 },
                rendering: { annual: 1980_00, perpetual: 6980_00 },
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
                perpetualRate: 0.2,
                perpetualMinimum: 9800_00,
                brandingRate: 0.15,
                brandingMinimum: 8000_00,
                oemRate: 0.15,
            },
        },
        USD: {
            currency: 'USD',
            locale: 'en-US',
            minimumStreams: 5,
            products: {
                remote: { annual: 179_00, perpetual: 629_00 },
                gaming: { annual: 379_00, perpetual: 1319_00 },
                rendering: { annual: 279_00, perpetual: 979_00 },
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
                perpetualRate: 0.2,
                perpetualMinimum: 1390_00,
                brandingRate: 0.15,
                brandingMinimum: 1120_00,
                oemRate: 0.15,
            },
        },
    },
}
