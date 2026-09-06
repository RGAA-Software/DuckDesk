package yun.pixels.client

import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class PixelsBrandTest {
    @Test
    fun applicationUsesFinalPixelsIdentity() {
        val context = ApplicationProvider.getApplicationContext<android.content.Context>()

        assertEquals("yun.pixels.client.debug", context.packageName)
        assertEquals("Pixels", context.getString(R.string.app_name))
    }
}
