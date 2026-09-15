package yun.pixels.client

import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.performClick
import androidx.test.espresso.Espresso.pressBack
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import yun.pixels.client.feature.devices.R as DevicesR
import yun.pixels.client.feature.cloudapps.R as CloudAppsR
import yun.pixels.client.feature.settings.R as SettingsR
import yun.pixels.client.feature.transfer.R as TransferR

class PixelsNavigationTest {
    @get:Rule
    val composeRule = createAndroidComposeRule<MainActivity>()

    @Test
    fun switchingTabsAlwaysOpensTheSelectedRootWithoutDuplicatingDestinations() {
        val activity = composeRule.activity
        val devicesContent = activity.getString(DevicesR.string.quick_connect)
        val transfersTab = activity.getString(R.string.navigation_transfers)
        val transfersContent = activity.getString(TransferR.string.transfer_requires_session)
        val settingsTab = activity.getString(R.string.navigation_settings)
        val settingsContent = activity.getString(SettingsR.string.account_title)
        val cloudAppsTab = activity.getString(R.string.navigation_cloud_apps)
        val cloudAppsContent = activity.getString(CloudAppsR.string.cloud_apps_title)

        composeRule.onNodeWithText(devicesContent).fetchSemanticsNode()
        composeRule.onNodeWithText(cloudAppsTab).performClick()
        composeRule.onNodeWithText(cloudAppsContent).fetchSemanticsNode()
        composeRule.onNodeWithText(cloudAppsTab).assertIsSelected()
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

        composeRule.onNodeWithText(activity.getString(R.string.navigation_devices)).performClick()
        composeRule.onNodeWithText(devicesContent).fetchSemanticsNode()
        composeRule.onAllNodesWithText(transfersContent).assertCountEquals(0)
        composeRule.onAllNodesWithText(settingsContent).assertCountEquals(0)
    }

    @Test
    fun cloudAppsIsAnIndependentTopLevelTab() {
        val activity = composeRule.activity
        val devicesTab = activity.getString(R.string.navigation_devices)
        val devicesContent = activity.getString(DevicesR.string.quick_connect)
        val cloudApps = activity.getString(R.string.navigation_cloud_apps)
        val cloudAppsTitle = activity.getString(CloudAppsR.string.cloud_apps_title)
        val settingsTab = activity.getString(R.string.navigation_settings)
        val settingsContent = activity.getString(SettingsR.string.account_title)

        composeRule.onNodeWithText(cloudApps).performClick()
        composeRule.onNodeWithText(cloudAppsTitle).fetchSemanticsNode()

        composeRule.onNodeWithText(settingsTab).performClick()
        composeRule.onNodeWithText(settingsContent).fetchSemanticsNode()

        composeRule.onNodeWithText(devicesTab).performClick()
        composeRule.onNodeWithText(devicesContent).fetchSemanticsNode()
        composeRule.onAllNodesWithText(cloudAppsTitle).assertCountEquals(0)
    }

    @Test
    fun backFromCloudAppsReturnsToDevices() {
        val activity = composeRule.activity
        val devicesContent = activity.getString(DevicesR.string.quick_connect)
        val cloudApps = activity.getString(R.string.navigation_cloud_apps)
        val cloudAppsTitle = activity.getString(CloudAppsR.string.cloud_apps_title)

        composeRule.onNodeWithText(cloudApps).performClick()
        composeRule.onNodeWithText(cloudAppsTitle).fetchSemanticsNode()
        pressBack()

        composeRule.onNodeWithText(devicesContent).fetchSemanticsNode()
        composeRule.onAllNodesWithText(cloudAppsTitle).assertCountEquals(0)
    }

    @Test
    fun backFromASecondaryTabReturnsHomeBeforeLeavingTheApplication() {
        val activity = composeRule.activity
        composeRule.onNodeWithText(activity.getString(R.string.navigation_settings)).performClick()

        pressBack()

        composeRule.onNodeWithText(activity.getString(DevicesR.string.quick_connect)).fetchSemanticsNode()
        composeRule.onNodeWithText(activity.getString(R.string.navigation_devices)).assertIsSelected()

        pressBack()

        composeRule.runOnIdle { assertTrue(activity.isFinishing) }
    }

    @Test
    fun backFromTransfersReturnsToDevices() {
        val activity = composeRule.activity
        composeRule.onNodeWithText(activity.getString(R.string.navigation_transfers)).performClick()

        pressBack()

        composeRule.onNodeWithText(activity.getString(DevicesR.string.quick_connect)).fetchSemanticsNode()
        composeRule.onNodeWithText(activity.getString(R.string.navigation_devices)).assertIsSelected()
    }
}
