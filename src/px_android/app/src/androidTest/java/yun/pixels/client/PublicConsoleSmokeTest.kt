package yun.pixels.client

import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTextReplacement
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assume.assumeTrue
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import yun.pixels.client.feature.cloudapps.R as CloudAppsR
import yun.pixels.client.feature.settings.R as SettingsR

@RunWith(AndroidJUnit4::class)
class PublicConsoleSmokeTest {
    @get:Rule
    val composeRule = createAndroidComposeRule<MainActivity>()

    @Test
    fun publicConsoleCanBeConfiguredAndRegistrationFormIsAvailable() {
        val activity = composeRule.activity
        composeRule.onNodeWithText(activity.getString(R.string.navigation_settings)).performClick()
        composeRule.onNodeWithText(activity.getString(SettingsR.string.console_endpoint)).performTextReplacement("https://39.71.45.66:4600")
        composeRule.onNodeWithText(activity.getString(SettingsR.string.test_connection)).performClick()
        composeRule.waitUntil(timeoutMillis = 10_000) {
            runCatching {
                composeRule.onNodeWithText(activity.getString(SettingsR.string.connection_succeeded)).fetchSemanticsNode()
            }.isSuccess
        }
        composeRule.onNodeWithText(activity.getString(SettingsR.string.save_endpoint)).performClick()
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
        assumeTrue("public test credentials were not supplied", !username.isNullOrBlank() && !password.isNullOrBlank())
        val requiredUsername = requireNotNull(username)
        val requiredPassword = requireNotNull(password)
        val activity = composeRule.activity

        composeRule.onNodeWithText(activity.getString(R.string.navigation_settings)).performClick()
        val alreadySignedIn = runCatching { composeRule.onNodeWithText(requiredUsername).fetchSemanticsNode() }.isSuccess
        if (!alreadySignedIn) {
            composeRule.onNodeWithText(activity.getString(SettingsR.string.console_endpoint))
                .performTextReplacement("https://39.71.45.66:4600")
            composeRule.onNodeWithText(activity.getString(SettingsR.string.save_endpoint)).performClick()
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
}
