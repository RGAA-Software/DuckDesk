package yun.pixels.client.feature.settings

import androidx.compose.material3.MaterialTheme
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import yun.pixels.client.core.domain.account.AccountProfile

class SettingsUpdateScreenTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun signedOutUserCannotStartUpdateDiscovery() {
        composeRule.setContent {
            MaterialTheme {
                SettingsScreen(
                    state = SettingsUiState(isLoading = false),
                    onAction = {},
                )
            }
        }

        composeRule.onNodeWithTag(UPDATE_CHECK_TEST_TAG).assertIsNotEnabled()
        composeRule.onAllNodesWithTag(UPDATE_INSTALL_TEST_TAG).assertCountEquals(0)
    }

    @Test
    fun availableVerifiedReleaseDispatchesInstallAction() {
        val dispatchedActions = mutableListOf<SettingsAction>()
        composeRule.setContent {
            MaterialTheme {
                SettingsScreen(
                    state = SettingsUiState(
                        isLoading = false,
                        profile = AccountProfile("user-1", "alice", null, false),
                        updateStatus = UpdateStatus.Available,
                        updateReleaseId = "11111111-1111-4111-8111-111111111111",
                        updateVersion = "1.2.3",
                        updateBuildNumber = 123,
                    ),
                    onAction = dispatchedActions::add,
                )
            }
        }

        composeRule.onNodeWithTag(UPDATE_CHECK_TEST_TAG).assertIsEnabled()
        composeRule.onNodeWithTag(UPDATE_INSTALL_TEST_TAG).performScrollTo().assertIsEnabled().performClick()
        assertEquals(listOf(SettingsAction.InstallUpdate), dispatchedActions)
    }
}
