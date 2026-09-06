package yun.pixels.client

import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.performClick
import androidx.test.espresso.Espresso.pressBack
import org.junit.Rule
import org.junit.Test
import yun.pixels.client.feature.devices.R as DevicesR
import yun.pixels.client.feature.settings.R as SettingsR
import yun.pixels.client.feature.transfer.R as TransferR

class PixelsNavigationTest {
    @get:Rule
    val composeRule = createAndroidComposeRule<MainActivity>()

    @Test
    fun switchingTabsRestoresStateWithoutDuplicatingDestinations() {
        val activity = composeRule.activity
        val devicesContent = activity.getString(DevicesR.string.quick_connect)
        val transfersTab = activity.getString(R.string.navigation_transfers)
        val transfersContent = activity.getString(TransferR.string.transfer_requires_session)
        val settingsTab = activity.getString(R.string.navigation_settings)
        val settingsContent = activity.getString(SettingsR.string.account_title)

        composeRule.onNodeWithText(devicesContent).fetchSemanticsNode()
        composeRule.onNodeWithText(settingsTab).performClick()
        composeRule.onNodeWithText(settingsContent).fetchSemanticsNode()
        composeRule.onNodeWithText(settingsTab).assertIsSelected()
        composeRule.onAllNodesWithText(devicesContent).assertCountEquals(0)
        composeRule.onNodeWithText(settingsTab).performClick()
        composeRule.onNodeWithText(settingsContent).fetchSemanticsNode()

        composeRule.onNodeWithText(transfersTab).performClick()
        composeRule.onNodeWithText(transfersContent).fetchSemanticsNode()
        composeRule.onNodeWithText(transfersTab).assertIsSelected()
        composeRule.onAllNodesWithText(settingsContent).assertCountEquals(0)
        composeRule.onNodeWithText(transfersTab).performClick()
        composeRule.onNodeWithText(transfersContent).fetchSemanticsNode()

        composeRule.onNodeWithText(settingsTab).performClick()
        composeRule.onNodeWithText(settingsContent).fetchSemanticsNode()
        pressBack()
        composeRule.onNodeWithText(devicesContent).fetchSemanticsNode()
        composeRule.onAllNodesWithText(settingsContent).assertCountEquals(0)

        composeRule.onNodeWithText(transfersTab).performClick()
        pressBack()
        composeRule.onNodeWithText(devicesContent).fetchSemanticsNode()
        composeRule.onAllNodesWithText(transfersContent).assertCountEquals(0)
    }

    @Test
    fun childPageIsRestoredAcrossTabsAndReselectReturnsToTabRoot() {
        val activity = composeRule.activity
        val devicesTab = activity.getString(R.string.navigation_devices)
        val devicesContent = activity.getString(DevicesR.string.quick_connect)
        val applications = activity.getString(DevicesR.string.applications)
        val applicationsTitle = activity.getString(DevicesR.string.applications_title)
        val settingsTab = activity.getString(R.string.navigation_settings)
        val settingsContent = activity.getString(SettingsR.string.account_title)

        composeRule.onNodeWithContentDescription(applications).performClick()
        composeRule.onNodeWithText(applicationsTitle).fetchSemanticsNode()

        composeRule.onNodeWithText(settingsTab).performClick()
        composeRule.onNodeWithText(settingsContent).fetchSemanticsNode()

        composeRule.onNodeWithText(devicesTab).performClick()
        composeRule.onNodeWithText(applicationsTitle).fetchSemanticsNode()
        composeRule.onAllNodesWithText(devicesContent).assertCountEquals(0)

        composeRule.onNodeWithText(devicesTab).performClick()
        composeRule.onNodeWithText(devicesContent).fetchSemanticsNode()
        composeRule.onAllNodesWithText(applicationsTitle).assertCountEquals(0)
    }

    @Test
    fun childPageBackReturnsToItsTabRoot() {
        val activity = composeRule.activity
        val devicesContent = activity.getString(DevicesR.string.quick_connect)
        val applications = activity.getString(DevicesR.string.applications)
        val applicationsTitle = activity.getString(DevicesR.string.applications_title)

        composeRule.onNodeWithContentDescription(applications).performClick()
        composeRule.onNodeWithText(applicationsTitle).fetchSemanticsNode()
        pressBack()

        composeRule.onNodeWithText(devicesContent).fetchSemanticsNode()
        composeRule.onAllNodesWithText(applicationsTitle).assertCountEquals(0)
    }
}
