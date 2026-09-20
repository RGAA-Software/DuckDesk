export type PricingCurrency = 'CNY' | 'USD'
export type PricingUsage = 'internal' | 'oem'
export type LicenseTerm = 'annual' | 'perpetual'
export type ProductKey = 'gaming' | 'rendering'
export type DeliveryKey = 'self' | 'remote' | 'custom'
export type BrandingKey = 'pixels' | 'basic' | 'full' | 'custom'

export interface ProductPrice {
    annual: number
    perpetual: number
}

export interface CurrencyCatalog {
    currency: PricingCurrency
    locale: string
    minimumStreams: number
    products: Record<ProductKey, ProductPrice>
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
        perpetualRate: number
        perpetualMinimum: number
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
    licenseTerm: LicenseTerm
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
    coreMaintenanceAmount: number
    brandingMaintenanceAmount: number
    recurringLicenseAmount: number
    oemMinimumAdjustmentAmount: number
    requiresDeliveryAssessment: boolean
    requiresCustomizationAssessment: boolean
    isStartingEstimate: boolean
}
