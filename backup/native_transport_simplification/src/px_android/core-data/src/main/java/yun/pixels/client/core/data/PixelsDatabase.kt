package yun.pixels.client.core.data

import android.content.Context
import androidx.room.Database
import androidx.room.Room
import androidx.room.RoomDatabase
import yun.pixels.client.core.domain.device.DeviceDirectory

@Database(
    entities = [StoredDevice::class],
    version = 1,
    exportSchema = false,
)
abstract class PixelsDatabase : RoomDatabase() {
    abstract fun deviceDao(): DeviceDao

    companion object {
        fun create(context: Context): PixelsDatabase = Room.databaseBuilder(
            context.applicationContext,
            PixelsDatabase::class.java,
            "pixels.db",
        ).build()
    }
}

fun createDeviceDirectory(context: Context): DeviceDirectory {
    val database = PixelsDatabase.create(context)
    return RoomDeviceDirectory(database.deviceDao())
}
