package yun.pixels.client.core.network

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.test.runTest
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountProfile
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountSession
import yun.pixels.client.core.domain.account.ConsoleEndpoint

class ConsoleAndroidUpdateTest {
    @Test
    fun latestUpdateUsesServerDerivedZeroParameterRouteAndAcceptsExactReleaseDomain() = runTest {
        var recordedPath: String? = null
        var recordedToken: String? = null
        val executor = ConsoleRequestExecutor { _, path, _, bearerToken, _, _ ->
            recordedPath = path
            recordedToken = bearerToken
            HttpResponse(200, release().toString())
        }
        val client = ConsoleApiClient(
            ioDispatcher = Dispatchers.Unconfined,
            requestExecutor = executor,
            androidReleaseIdentity = customerIdentity(),
        )

        val result = client.latestAndroidUpdate(session())

        require(result is AccountResult.Success)
        assertEquals("/api/console/updates/latest", recordedPath)
        assertEquals("account-token", recordedToken)
        assertEquals(32_018L, result.value.artifact.buildNumber)
        assertEquals("3.2.18", result.value.artifact.version)
        assertEquals(1L, result.value.repositoryRootVersion)
        assertEquals("b".repeat(64), result.value.artifact.platformSignerSha256)
    }

    @Test
    fun releaseCannotReplaceAnyServerDerivedIdentityOrUseSafeButWrongTargetPath() {
        val mutations = listOf<(JSONObject) -> Unit>(
            { payload -> payload.getJSONObject("artifact").getJSONObject("target").put("product", "client") },
            { payload -> payload.getJSONObject("artifact").getJSONObject("target").put("distribution", "official") },
            { payload -> payload.getJSONObject("artifact").getJSONObject("target").put("release_namespace", "pixels.official") },
            { payload -> payload.getJSONObject("artifact").getJSONObject("target").put("channel", "preview") },
            { payload -> payload.getJSONObject("artifact").getJSONObject("target").put("architecture", "x86_64") },
            { payload -> payload.getJSONObject("artifact").put("target_name", "android/android/official/stable/aarch64/32018/app.apk") },
            { payload -> payload.getJSONObject("artifact").put("target_name", "android/android/customer/stable/aarch64/32019/app.apk") },
        )

        mutations.forEach { mutate ->
            val payload = JSONObject(release().toString())
            mutate(payload)
            val result = parseAndroidUpdateRelease(payload, customerIdentity())
            assertEquals(AccountResult.Failure(AccountFailure.InvalidResponse), result)
        }
    }

    @Test
    fun catalogParsingRejectsUnknownFieldsNonCanonicalNumbersAndInvalidRepositoryMetadata() {
        val mutations = listOf<(JSONObject) -> Unit>(
            { payload -> payload.put("unexpected", true) },
            { payload -> payload.put("repository_root_version", 1.0) },
            { payload -> payload.getJSONObject("artifact").put("build_number", 32_018.0) },
            { payload -> payload.getJSONObject("artifact").put("metadata_base_url", "https://user@example.test/metadata/") },
            { payload -> payload.getJSONObject("artifact").put("platform_signer_sha256", "B".repeat(64)) },
            { payload -> payload.getJSONObject("artifact").getJSONObject("target").put("oem_id", 7) },
        )

        mutations.forEach { mutate ->
            val payload = JSONObject(release().toString())
            mutate(payload)
            assertEquals(
                AccountResult.Failure(AccountFailure.InvalidResponse),
                parseAndroidUpdateRelease(payload, customerIdentity()),
            )
        }
    }

    @Test
    fun officialCustomerAndOemReleaseIdentityRulesAreMutuallyExclusive() {
        assertNotNull(AndroidReleaseIdentity.create("official", "pixels.official", null))
        assertNotNull(AndroidReleaseIdentity.create("customer", "pixels.customer", null))
        assertNotNull(AndroidReleaseIdentity.create("oem", "oem.acme-cloud", "acme-cloud"))
        assertNull(AndroidReleaseIdentity.create("official", "pixels.customer", null))
        assertNull(AndroidReleaseIdentity.create("customer", "pixels.customer", "acme-cloud"))
        assertNull(AndroidReleaseIdentity.create("oem", "oem.other", "acme-cloud"))
        assertNull(AndroidReleaseIdentity.create("oem", "oem.pixels", "pixels"))
    }

    @Test
    fun clientWithoutBuildBoundReleaseIdentityFailsClosed() = runTest {
        val client = ConsoleApiClient(
            Dispatchers.Unconfined,
            ConsoleRequestExecutor { _, _, _, _, _, _ -> HttpResponse(200, release().toString()) },
        )

        val result = client.latestAndroidUpdate(session())

        assertTrue(result is AccountResult.Failure)
        assertEquals(AccountFailure.InvalidResponse, (result as AccountResult.Failure).reason)
    }

    private fun customerIdentity(): AndroidReleaseIdentity = requireNotNull(
        AndroidReleaseIdentity.create("customer", "pixels.customer", null),
    )

    private fun session() = AccountSession(
        ConsoleEndpoint("https://console.example"),
        AccountProfile("user-1", "alice", null, false),
        "account-token",
        Long.MAX_VALUE,
    )

    private fun release() = JSONObject(
        """
        {
          "id":"10000000-0000-0000-0000-000000000001",
          "artifact":{
            "target":{
              "product":"android",
              "distribution":"customer",
              "release_namespace":"pixels.customer",
              "oem_id":null,
              "channel":"stable",
              "os":"android",
              "architecture":"aarch64"
            },
            "build_number":32018,
            "version":"3.2.18",
            "metadata_base_url":"https://downloads.example.test/metadata/",
            "targets_base_url":"https://downloads.example.test/targets/",
            "target_name":"android/android/customer/stable/aarch64/32018/pixels-3.2.18.apk",
            "sha256":"${"a".repeat(64)}",
            "platform_signer_sha256":"${"b".repeat(64)}",
            "size_bytes":12345678
          },
          "repository_publication_sha256":"${"c".repeat(64)}",
          "repository_root_version":1,
          "state":"approved",
          "revision":2,
          "created_at":"2026-09-22T01:00:00Z",
          "updated_at":"2026-09-22T01:01:00Z"
        }
        """.trimIndent(),
    )
}
