export type PricingCurrency = 'CNY' | 'USD'
export type PricingUsage = 'internal' | 'oem'
export type ProductKey = 'gaming' | 'rendering'
export type DeliveryKey = 'self' | 'remote' | 'custom'
export type BrandingKey = 'pixels' | 'basic' | 'full' | 'custom'

export interface CurrencyCatalog {
    currency: PricingCurrency
    locale: string
    minimumStreams: number
    products: Record<ProductKey, number>
    delivery: {
        remote: number
    }
    branding: {
        basic: number
        full: number
        custom: number
    }
    oem: {
        setup: number
        annualMinimum: number
        streamMultiplier: number
    }
    maintenance: {
        brandingRate: number
        brandingMinimum: number
        oemRate: number
    }
}

export interface PriceCatalog {
    version: string
    effectiveFrom: string
    currencies: Record<PricingCurrency, CurrencyCatalog>
}

export interface PricingSelection {
    usage: PricingUsage
    currency: PricingCurrency
    quantities: Record<ProductKey, number>
    delivery: DeliveryKey
    branding: BrandingKey
}

export interface ProductLineItem {
    product: ProductKey
    quantity: number
    unitPrice: number
    amount: number
}

export interface PricingEstimate {
    catalogVersion: string
    effectiveFrom: string
    currency: PricingCurrency
    productLines: ProductLineItem[]
    licenseAmount: number
    deliveryAmount: number
    brandingAmount: number
    oneTimeAmount: number
    firstYearAmount: number
    secondYearAmount: number
    brandingMaintenanceAmount: number
    recurringLicenseAmount: number
    oemMinimumAdjustmentAmount: number
    requiresDeliveryAssessment: boolean
    requiresCustomizationAssessment: boolean
    isStartingEstimate: boolean
}
