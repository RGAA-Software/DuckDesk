import { priceCatalog } from './catalog'
import type {
    BrandingKey,
    CurrencyCatalog,
    PricingEstimate,
    PricingSelection,
    ProductKey,
} from './types'

const productKeys: ProductKey[] = ['gaming', 'rendering']

function roundMoney(amount: number): number {
    return Math.round(amount)
}

function brandingPrice(
    branding: BrandingKey,
    catalog: CurrencyCatalog,
): number {
    if (branding === 'basic') return catalog.branding.basic
    if (branding === 'full') return catalog.branding.full
    if (branding === 'custom') return catalog.branding.custom
    return 0
}

export function normalizeQuantity(
    quantity: number,
    minimumStreams: number,
): number {
    if (!Number.isFinite(quantity) || quantity <= 0) return 0
    return Math.max(minimumStreams, Math.floor(quantity))
}

export function calculatePricing(selection: PricingSelection): PricingEstimate {
    const catalog = priceCatalog.currencies[selection.currency]
    const licenseTerm = selection.usage === 'oem' ? 'annual' : selection.licenseTerm

    const productLines = productKeys.flatMap((product) => {
        const quantity = normalizeQuantity(
            selection.quantities[product],
            catalog.minimumStreams,
        )
        if (quantity === 0) return []

        const unitPrice = catalog.products[product][licenseTerm]
        return [{ product, quantity, unitPrice, amount: unitPrice * quantity }]
    })

    const baseLicenseAmount = productLines.reduce(
        (total, productLine) => total + productLine.amount,
        0,
    )
    const deliveryAmount =
        selection.delivery === 'remote' ? catalog.delivery.remote : 0
    const selectedBrandingAmount = brandingPrice(selection.branding, catalog)
    const requiresDeliveryAssessment = selection.delivery === 'custom'
    const requiresCustomizationAssessment = selection.branding === 'custom'

    if (selection.usage === 'oem') {
        const oemProductLines = productLines.map((productLine) => {
            const unitPrice = roundMoney(
                productLine.unitPrice * catalog.oem.streamMultiplier,
            )
            return {
                ...productLine,
                unitPrice,
                amount: unitPrice * productLine.quantity,
            }
        })
        const oemStreamAmount = oemProductLines.reduce(
            (total, productLine) => total + productLine.amount,
            0,
        )
        const recurringLicenseAmount = Math.max(
            oemStreamAmount,
            catalog.oem.annualMinimum,
        )
        const oemMinimumAdjustmentAmount =
            recurringLicenseAmount - oemStreamAmount
        const brandingAmount =
            selection.branding === 'custom' ? selectedBrandingAmount : 0
        const oneTimeAmount = catalog.oem.setup + deliveryAmount + brandingAmount
        const brandingMaintenanceAmount = roundMoney(
            (catalog.oem.setup + brandingAmount) * catalog.maintenance.oemRate,
        )

        return {
            catalogVersion: priceCatalog.version,
            effectiveFrom: priceCatalog.effectiveFrom,
            currency: selection.currency,
            productLines: oemProductLines,
            licenseAmount: recurringLicenseAmount,
            deliveryAmount,
            brandingAmount: catalog.oem.setup + brandingAmount,
            oneTimeAmount,
            firstYearAmount: oneTimeAmount + recurringLicenseAmount,
            secondYearAmount:
                recurringLicenseAmount + brandingMaintenanceAmount,
            coreMaintenanceAmount: 0,
            brandingMaintenanceAmount,
            recurringLicenseAmount,
            oemMinimumAdjustmentAmount,
            requiresDeliveryAssessment,
            requiresCustomizationAssessment,
            isStartingEstimate: true,
        }
    }

    const brandingAmount = selectedBrandingAmount
    const brandingMaintenanceAmount =
        brandingAmount > 0
            ? Math.max(
                  roundMoney(
                      brandingAmount * catalog.maintenance.brandingRate,
                  ),
                  catalog.maintenance.brandingMinimum,
              )
            : 0

    if (licenseTerm === 'annual') {
        const oneTimeAmount = deliveryAmount + brandingAmount
        return {
            catalogVersion: priceCatalog.version,
            effectiveFrom: priceCatalog.effectiveFrom,
            currency: selection.currency,
            productLines,
            licenseAmount: baseLicenseAmount,
            deliveryAmount,
            brandingAmount,
            oneTimeAmount,
            firstYearAmount: baseLicenseAmount + oneTimeAmount,
            secondYearAmount:
                baseLicenseAmount + brandingMaintenanceAmount,
            coreMaintenanceAmount: 0,
            brandingMaintenanceAmount,
            recurringLicenseAmount: baseLicenseAmount,
            oemMinimumAdjustmentAmount: 0,
            requiresDeliveryAssessment,
            requiresCustomizationAssessment,
            isStartingEstimate:
                requiresDeliveryAssessment || requiresCustomizationAssessment,
        }
    }

    const coreMaintenanceAmount = Math.max(
        roundMoney(baseLicenseAmount * catalog.maintenance.perpetualRate),
        catalog.maintenance.perpetualMinimum,
    )
    const oneTimeAmount = baseLicenseAmount + deliveryAmount + brandingAmount
    return {
        catalogVersion: priceCatalog.version,
        effectiveFrom: priceCatalog.effectiveFrom,
        currency: selection.currency,
        productLines,
        licenseAmount: baseLicenseAmount,
        deliveryAmount,
        brandingAmount,
        oneTimeAmount,
        firstYearAmount: oneTimeAmount,
        secondYearAmount: coreMaintenanceAmount + brandingMaintenanceAmount,
        coreMaintenanceAmount,
        brandingMaintenanceAmount,
        recurringLicenseAmount: 0,
        oemMinimumAdjustmentAmount: 0,
        requiresDeliveryAssessment,
        requiresCustomizationAssessment,
        isStartingEstimate:
            requiresDeliveryAssessment || requiresCustomizationAssessment,
    }
}

export function formatMoney(amount: number, currency: 'CNY' | 'USD'): string {
    const catalog = priceCatalog.currencies[currency]
    return new Intl.NumberFormat(catalog.locale, {
        style: 'currency',
        currency,
        maximumFractionDigits: 0,
    }).format(amount / 100)
}
