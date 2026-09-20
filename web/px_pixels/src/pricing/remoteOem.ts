import { priceCatalog } from './catalog'
import type { PricingCurrency } from './types'

interface RemoteOemCurrencyCatalog {
    locale: string
    annualStreamPrice: number
    annualMinimum: number
    onboardingPrice: number
    maintenanceRate: number
}

export interface RemoteOemEstimate {
    currency: PricingCurrency
    concurrentStreams: number
    annualStreamPrice: number
    calculatedStreamAmount: number
    annualLicenseAmount: number
    minimumAdjustmentAmount: number
    onboardingAmount: number
    annualMaintenanceAmount: number
    firstYearAmount: number
    renewalAmount: number
}

export const remoteOemCatalog: Record<
    PricingCurrency,
    RemoteOemCurrencyCatalog
> = {
    CNY: {
        locale: 'zh-CN',
        annualStreamPrice: 1920_00,
        annualMinimum: 100000_00,
        onboardingPrice: 198000_00,
        maintenanceRate: 0.15,
    },
    USD: {
        locale: 'en-US',
        annualStreamPrice: 269_00,
        annualMinimum: 14000_00,
        onboardingPrice: 27800_00,
        maintenanceRate: 0.15,
    },
}

export function normalizeRemoteOemStreams(concurrentStreams: number): number {
    if (!Number.isFinite(concurrentStreams)) return 1
    return Math.max(1, Math.floor(concurrentStreams))
}

export function calculateRemoteOem(
    concurrentStreams: number,
    currency: PricingCurrency,
): RemoteOemEstimate {
    const catalog = remoteOemCatalog[currency]
    const normalizedStreams = normalizeRemoteOemStreams(concurrentStreams)
    const calculatedStreamAmount =
        normalizedStreams * catalog.annualStreamPrice
    const annualLicenseAmount = Math.max(
        calculatedStreamAmount,
        catalog.annualMinimum,
    )
    const annualMaintenanceAmount = Math.round(
        catalog.onboardingPrice * catalog.maintenanceRate,
    )

    return {
        currency,
        concurrentStreams: normalizedStreams,
        annualStreamPrice: catalog.annualStreamPrice,
        calculatedStreamAmount,
        annualLicenseAmount,
        minimumAdjustmentAmount:
            annualLicenseAmount - calculatedStreamAmount,
        onboardingAmount: catalog.onboardingPrice,
        annualMaintenanceAmount,
        firstYearAmount: annualLicenseAmount + catalog.onboardingPrice,
        renewalAmount: annualLicenseAmount + annualMaintenanceAmount,
    }
}

export function formatRemoteOemMoney(
    amount: number,
    currency: PricingCurrency,
): string {
    const catalog = remoteOemCatalog[currency]
    return new Intl.NumberFormat(catalog.locale, {
        style: 'currency',
        currency,
        maximumFractionDigits: 0,
    }).format(amount / 100)
}

export const remoteOemPriceBook = {
    version: priceCatalog.version,
    effectiveFrom: priceCatalog.effectiveFrom,
}
