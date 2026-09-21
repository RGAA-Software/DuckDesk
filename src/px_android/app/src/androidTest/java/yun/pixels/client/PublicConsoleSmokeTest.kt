package yun.pixels.client

import android.os.SystemClock
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onAllNodesWithContentDescription
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextReplacement
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assume.assumeTrue
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import yun.pixels.client.feature.cloudapps.R as CloudAppsR
import yun.pixels.client.feature.devices.R as DevicesR
import yun.pixels.client.feature.remote.R as RemoteR
import yun.pixels.client.feature.remote.RemoteVideoSizeSemanticsKey
import yun.pixels.client.feature.settings.R as SettingsR

@RunWith(AndroidJUnit4::class)
class PublicConsoleSmokeTest {
    @get:Rule
    val composeRule = createAndroidComposeRule<MainActivity>()

    @Test
    fun distributionEndpointPolicyAndRegistrationFormAreAvailable() {
        val activity = composeRule.activity
        composeRule.onNodeWithText(activity.getString(R.string.navigation_settings)).performClick()
        if (BuildConfig.DEPLOYMENT_DISTRIBUTION == "official") {
            composeRule.onNodeWithText(activity.getString(SettingsR.string.official_console_endpoint)).fetchSemanticsNode()
            composeRule.onAllNodesWithText(activity.getString(SettingsR.string.console_endpoint)).assertCountEquals(0)
        } else {
            composeRule.onNodeWithText(activity.getString(SettingsR.string.console_endpoint)).fetchSemanticsNode()
        }
        if (composeRule.onAllNodesWithText(activity.getString(SettingsR.string.sign_out)).fetchSemanticsNodes().isNotEmpty()) {
            composeRule.onNodeWithText(activity.getString(SettingsR.string.sign_out)).performClick()
            composeRule.waitUntil(timeoutMillis = 10_000) {
                composeRule.onAllNodesWithText(activity.getString(SettingsR.string.need_account)).fetchSemanticsNodes().isNotEmpty()
            }
        }
        composeRule.onNodeWithText(activity.getString(SettingsR.string.need_account)).performClick()
        composeRule.onNodeWithText(activity.getString(SettingsR.string.confirm_password)).fetchSemanticsNode()
        composeRule.onNodeWithText(activity.getString(SettingsR.string.create_account)).fetchSemanticsNode()
    }

    @Test
    fun publicAccountCanSignInAndLoadCloudApps() {
        val arguments = InstrumentationRegistry.getArguments()
        val username = arguments.getString("username")
        val password = arguments.getString("password")
        val consoleEndpoint = arguments.getString("consoleEndpoint")
        assumeTrue("public test credentials were not supplied", !username.isNullOrBlank() && !password.isNullOrBlank())
        assumeTrue(
            "customer test Console endpoint was not supplied",
            BuildConfig.DEPLOYMENT_DISTRIBUTION == "official" || !consoleEndpoint.isNullOrBlank(),
        )
        val requiredUsername = requireNotNull(username)
        val requiredPassword = requireNotNull(password)
        val activity = composeRule.activity

        composeRule.onNodeWithText(activity.getString(R.string.navigation_settings)).performClick()
        val alreadySignedIn = composeRule.onAllNodesWithText(activity.getString(SettingsR.string.sign_out)).fetchSemanticsNodes().isNotEmpty()
        if (!alreadySignedIn) {
            if (BuildConfig.DEPLOYMENT_DISTRIBUTION == "customer") {
                composeRule.onNodeWithText(activity.getString(SettingsR.string.console_endpoint))
                    .performTextReplacement(requireNotNull(consoleEndpoint))
                composeRule.onNodeWithText(activity.getString(SettingsR.string.save_endpoint)).performClick()
            }
            composeRule.onNodeWithText(activity.getString(SettingsR.string.username)).performTextReplacement(requiredUsername)
            composeRule.onNodeWithText(activity.getString(SettingsR.string.password)).performTextReplacement(requiredPassword)
            composeRule.onNodeWithText(activity.getString(SettingsR.string.sign_in)).performClick()
            composeRule.waitUntil(timeoutMillis = 15_000) {
                runCatching { composeRule.onNodeWithText(requiredUsername).fetchSemanticsNode() }.isSuccess
            }
        }

        composeRule.onNodeWithText(activity.getString(R.string.navigation_cloud_apps)).performClick()
        assertTrue(composeRule.onAllNodesWithText(activity.getString(CloudAppsR.string.cloud_apps_title)).fetchSemanticsNodes().isNotEmpty())
        assertTrue(
            runCatching { composeRule.onNodeWithText(activity.getString(CloudAppsR.string.sign_in)).fetchSemanticsNode() }.isFailure,
        )
    }

    @Test
    fun publicCloudApplicationProducesDecodedFramesAndStopsCleanly() {
        val arguments = InstrumentationRegistry.getArguments()
        val username = arguments.getString("username")
        val password = arguments.getString("password")
        val applicationName = arguments.getString("applicationName") ?: "Pixels Public Acceptance Webview"
        assumeTrue("public test credentials were not supplied", !username.isNullOrBlank() && !password.isNullOrBlank())
        signInIfNeeded(requireNotNull(username), requireNotNull(password), arguments.getString("consoleEndpoint"))

        val activity = composeRule.activity
        composeRule.onNodeWithText(activity.getString(R.string.navigation_cloud_apps)).performClick()
        waitForCloudApplication(activity, applicationName)
        val connectLabel = activity.getString(CloudAppsR.string.connect)
        val startLabel = activity.getString(CloudAppsR.string.start)
        val connectButtons = composeRule.onAllNodesWithText(connectLabel).fetchSemanticsNodes()
        composeRule.onNodeWithText(if (connectButtons.isEmpty()) startLabel else connectLabel).performClick()

        try {
            composeRule.waitUntil(timeoutMillis = 90_000) {
                runCatching {
                    val videoSize = composeRule.onNode(
                        matcher = SemanticsMatcher.keyIsDefined(RemoteVideoSizeSemanticsKey),
                        useUnmergedTree = true,
                    )
                        .fetchSemanticsNode()
                        .config[RemoteVideoSizeSemanticsKey]
                    Regex("^[1-9][0-9]*x[1-9][0-9]*$").matches(videoSize)
                }.getOrDefault(false)
            }
        } finally {
            endRemoteSessionIfVisible(activity)
        }
    }

    @Test
    fun publicCloudApplicationProducesDecodedFramesThroughRelayAndStopsCleanly() {
        val arguments = InstrumentationRegistry.getArguments()
        val username = arguments.getString("username")
        val password = arguments.getString("password")
        val applicationName = arguments.getString("applicationName") ?: "Pixels Public Acceptance Webview"
        assumeTrue("public test credentials were not supplied", !username.isNullOrBlank() && !password.isNullOrBlank())
        signInIfNeeded(requireNotNull(username), requireNotNull(password), arguments.getString("consoleEndpoint"))

        val activity = composeRule.activity
        composeRule.onNodeWithText(activity.getString(R.string.navigation_cloud_apps)).performClick()
        waitForCloudApplication(activity, applicationName)
        setCloudApplicationRoute(activity, DevicesR.string.connection_route_relay)
        val connectLabel = activity.getString(CloudAppsR.string.connect)
        val startLabel = activity.getString(CloudAppsR.string.start)
        val connectButtons = composeRule.onAllNodesWithText(connectLabel).fetchSemanticsNodes()
        composeRule.onNodeWithText(if (connectButtons.isEmpty()) startLabel else connectLabel).performClick()

        try {
            awaitDecodedFrame()
            val holdAfterFrameMillis = arguments.getString("holdAfterFrameMillis")?.toLongOrNull()?.coerceIn(0L, 30_000L) ?: 0L
            if (holdAfterFrameMillis > 0L) {
                SystemClock.sleep(holdAfterFrameMillis)
                awaitDecodedFrame()
            }
        } finally {
            endRemoteSessionIfVisible(activity)
            if (hasText(activity.getString(CloudAppsR.string.cloud_apps_title))) {
                setCloudApplicationRoute(activity, DevicesR.string.connection_route_automatic)
            }
        }
    }

    private fun signInIfNeeded(username: String, password: String, consoleEndpoint: String?) {
        val activity = composeRule.activity
        composeRule.onNodeWithText(activity.getString(R.string.navigation_settings)).performClick()
        if (composeRule.onAllNodesWithText(activity.getString(SettingsR.string.sign_out)).fetchSemanticsNodes().isNotEmpty()) return
        if (BuildConfig.DEPLOYMENT_DISTRIBUTION == "customer") {
            composeRule.onNodeWithText(activity.getString(SettingsR.string.console_endpoint))
                .performTextReplacement(requireNotNull(consoleEndpoint))
            composeRule.onNodeWithText(activity.getString(SettingsR.string.save_endpoint)).performClick()
        }
        composeRule.onNodeWithText(activity.getString(SettingsR.string.username)).performTextReplacement(username)
        composeRule.onNodeWithText(activity.getString(SettingsR.string.password)).performTextReplacement(password)
        composeRule.onNodeWithText(activity.getString(SettingsR.string.sign_in)).performClick()
        composeRule.waitUntil(timeoutMillis = 15_000) {
            composeRule.onAllNodesWithText(activity.getString(SettingsR.string.sign_out)).fetchSemanticsNodes().isNotEmpty()
        }
    }

    private fun waitForCloudApplication(activity: MainActivity, applicationName: String) {
        val continueLabel = activity.getString(CloudAppsR.string.continue_action)
        repeat(3) {
            runCatching {
                composeRule.waitUntil(timeoutMillis = 20_000) {
                    composeRule.onAllNodesWithText(applicationName).fetchSemanticsNodes().isNotEmpty() ||
                        composeRule.onAllNodesWithText(continueLabel).fetchSemanticsNodes().isNotEmpty()
                }
            }
            if (composeRule.onAllNodesWithText(applicationName).fetchSemanticsNodes().isNotEmpty()) return
            if (composeRule.onAllNodesWithText(continueLabel).fetchSemanticsNodes().isNotEmpty()) {
                composeRule.onNodeWithText(continueLabel).performClick()
                runCatching {
                    composeRule.waitUntil(timeoutMillis = 10_000) {
                        composeRule.onAllNodesWithText(applicationName).fetchSemanticsNodes().isNotEmpty()
                    }
                }
                if (composeRule.onAllNodesWithText(applicationName).fetchSemanticsNodes().isNotEmpty()) return
            }
        }
        composeRule.onNodeWithText(applicationName).fetchSemanticsNode()
    }

    private fun setCloudApplicationRoute(activity: MainActivity, routeResource: Int) {
        val preferencesDescription = activity.getString(CloudAppsR.string.connection_preferences)
        composeRule.onNodeWithContentDescription(preferencesDescription).performClick()
        val routeLabel = activity.getString(routeResource)
        composeRule.waitUntil(timeoutMillis = 10_000) {
            composeRule.onAllNodesWithText(routeLabel).fetchSemanticsNodes().isNotEmpty()
        }
        composeRule.onNodeWithText(routeLabel).performClick()
        composeRule.onNodeWithText(activity.getString(DevicesR.string.save)).performClick()
        composeRule.waitUntil(timeoutMillis = 10_000) {
            composeRule.onAllNodesWithContentDescription(preferencesDescription).fetchSemanticsNodes().isNotEmpty()
        }
    }

    private fun awaitDecodedFrame() {
        composeRule.waitUntil(timeoutMillis = 90_000) {
            runCatching {
                val videoSize = composeRule.onNode(
                    matcher = SemanticsMatcher.keyIsDefined(RemoteVideoSizeSemanticsKey),
                    useUnmergedTree = true,
                )
                    .fetchSemanticsNode()
                    .config[RemoteVideoSizeSemanticsKey]
                Regex("^[1-9][0-9]*x[1-9][0-9]*$").matches(videoSize)
            }.getOrDefault(false)
        }
    }

    private fun endRemoteSessionIfVisible(activity: MainActivity) {
        val expandDescription = activity.getString(RemoteR.string.remote_controls_expand)
        if (runCatching { composeRule.onNodeWithContentDescription(expandDescription).fetchSemanticsNode() }.isSuccess) {
            composeRule.onNodeWithContentDescription(expandDescription).performClick()
        }
        val endDescription = activity.getString(RemoteR.string.remote_exit)
        if (runCatching { composeRule.onNodeWithContentDescription(endDescription).fetchSemanticsNode() }.isSuccess) {
            composeRule.onNodeWithContentDescription(endDescription).performScrollTo().performClick()
            composeRule.waitUntil(timeoutMillis = 30_000) {
                hasText(endDescription) || hasText(activity.getString(CloudAppsR.string.cloud_apps_title))
            }
            if (hasText(endDescription)) {
                composeRule.onNodeWithText(endDescription).performClick()
            }
            composeRule.waitUntil(timeoutMillis = 30_000) {
                hasText(activity.getString(CloudAppsR.string.cloud_apps_title))
            }
        }
        val stopLabel = activity.getString(CloudAppsR.string.stop)
        if (hasText(stopLabel)) {
            composeRule.onNodeWithText(stopLabel).performClick()
            composeRule.waitUntil(timeoutMillis = 30_000) {
                hasText(activity.getString(CloudAppsR.string.start))
            }
        }
    }

    private fun hasText(text: String): Boolean = runCatching {
        composeRule.onAllNodesWithText(text).fetchSemanticsNodes().isNotEmpty()
    }.getOrDefault(false)
}
