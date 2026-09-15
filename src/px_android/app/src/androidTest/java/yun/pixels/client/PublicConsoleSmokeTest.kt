package yun.pixels.client

import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTextReplacement
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
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
        composeRule.onNodeWithText(activity.getString(SettingsR.string.need_account)).performClick()
        composeRule.onNodeWithText(activity.getString(SettingsR.string.confirm_password)).fetchSemanticsNode()
        composeRule.onNodeWithText(activity.getString(SettingsR.string.create_account)).fetchSemanticsNode()
    }
}
