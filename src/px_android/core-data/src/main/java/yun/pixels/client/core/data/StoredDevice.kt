package yun.pixels.client.core.data

import androidx.room.Dao
import androidx.room.Entity
import androidx.room.PrimaryKey
import androidx.room.Query
import androidx.room.Upsert
import kotlinx.coroutines.flow.Flow

@Entity(tableName = "devices")
data class StoredDevice(
    @PrimaryKey val deviceId: String,
    val displayName: String,
    val platformName: String,
    val host: String,
    val panelPort: Int,
    val renderPort: Int,
    val encryptedCredential: String?,
    val lastConnectedEpochMillis: Long?,
    val lastSeenEpochMillis: Long?,
)

@Dao
interface DeviceDao {
    @Query("SELECT * FROM devices ORDER BY COALESCE(lastConnectedEpochMillis, lastSeenEpochMillis, 0) DESC, displayName COLLATE NOCASE")
    fun observeAll(): Flow<List<StoredDevice>>

    @Query("SELECT encryptedCredential FROM devices WHERE deviceId = :deviceId")
    suspend fun encryptedCredential(deviceId: String): String?

    @Upsert
    suspend fun upsert(device: StoredDevice)

    @Query("DELETE FROM devices WHERE deviceId = :deviceId")
    suspend fun delete(deviceId: String)
}
