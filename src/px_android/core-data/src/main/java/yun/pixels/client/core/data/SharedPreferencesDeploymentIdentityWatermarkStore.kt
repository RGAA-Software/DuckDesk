package yun.pixels.client.core.data

import android.content.Context
import android.content.SharedPreferences
import java.util.UUID
import yun.pixels.client.core.domain.account.DeploymentIdentityWatermark
import yun.pixels.client.core.domain.account.DeploymentIdentityWatermarkState
import yun.pixels.client.core.domain.account.DeploymentIdentityWatermarkStore

class SharedPreferencesDeploymentIdentityWatermarkStore private constructor(
    private val preferences: SharedPreferences,
) : DeploymentIdentityWatermarkStore {
    private val lock = Any()

    override fun load(): DeploymentIdentityWatermarkState = synchronized(lock) {
        runCatching(::loadUnsafe).getOrDefault(DeploymentIdentityWatermarkState.Invalid)
    }

    private fun loadUnsafe(): DeploymentIdentityWatermarkState {
        if (!preferences.contains(SCHEMA_VERSION)) return DeploymentIdentityWatermarkState.Empty
        val schemaVersion = preferences.getLong(SCHEMA_VERSION, 0)
        val deploymentId = preferences.getString(DEPLOYMENT_ID, null)
        val deploymentKind = preferences.getString(DEPLOYMENT_KIND, null)
        val certificateVersion = preferences.getLong(CERTIFICATE_VERSION, 0)
        val descriptorRevision = preferences.getLong(DESCRIPTOR_REVISION, 0)
        val trustEpoch = preferences.getLong(TRUST_EPOCH, 0)
        val canonicalDeploymentId = deploymentId?.let { value ->
            runCatching { UUID.fromString(value) }.getOrNull()?.toString()?.takeIf { it == value }
        }
        if (
            preferences.all.keys != REQUIRED_KEYS || schemaVersion != CURRENT_SCHEMA_VERSION || canonicalDeploymentId == null ||
            deploymentKind !in VALID_KINDS || certificateVersion <= 0 || descriptorRevision <= 0 || trustEpoch <= 0
        ) {
            return DeploymentIdentityWatermarkState.Invalid
        }
        return DeploymentIdentityWatermarkState.Present(
            DeploymentIdentityWatermark(
                canonicalDeploymentId,
                requireNotNull(deploymentKind),
                certificateVersion,
                descriptorRevision,
                trustEpoch,
            ),
        )
    }

    override fun save(watermark: DeploymentIdentityWatermark): Boolean = synchronized(lock) {
        if (!watermark.isValid()) return@synchronized false
        preferences.edit()
            .clear()
            .putLong(SCHEMA_VERSION, CURRENT_SCHEMA_VERSION)
            .putString(DEPLOYMENT_ID, watermark.deploymentId)
            .putString(DEPLOYMENT_KIND, watermark.deploymentKind)
            .putLong(CERTIFICATE_VERSION, watermark.certificateVersion)
            .putLong(DESCRIPTOR_REVISION, watermark.descriptorRevision)
            .putLong(TRUST_EPOCH, watermark.trustEpoch)
            .commit()
    }

    companion object {
        fun create(context: Context): SharedPreferencesDeploymentIdentityWatermarkStore =
            SharedPreferencesDeploymentIdentityWatermarkStore(
                context.applicationContext.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE),
            )

        private fun DeploymentIdentityWatermark.isValid(): Boolean =
            runCatching {
                val identifier = UUID.fromString(deploymentId)
                identifier.toString() == deploymentId && identifier != UUID(0, 0)
            }.getOrDefault(false) &&
                deploymentKind in VALID_KINDS && certificateVersion > 0 && descriptorRevision > 0 && trustEpoch > 0

        private const val PREFERENCES_NAME = "pixels_deployment_identity_watermark_v1"
        private const val SCHEMA_VERSION = "schema_version"
        private const val DEPLOYMENT_ID = "deployment_id"
        private const val DEPLOYMENT_KIND = "deployment_kind"
        private const val CERTIFICATE_VERSION = "certificate_version"
        private const val DESCRIPTOR_REVISION = "descriptor_revision"
        private const val TRUST_EPOCH = "trust_epoch"
        private const val CURRENT_SCHEMA_VERSION = 1L
        private val VALID_KINDS = setOf("official", "private")
        private val REQUIRED_KEYS = setOf(
            SCHEMA_VERSION,
            DEPLOYMENT_ID,
            DEPLOYMENT_KIND,
            CERTIFICATE_VERSION,
            DESCRIPTOR_REVISION,
            TRUST_EPOCH,
        )
    }
}
