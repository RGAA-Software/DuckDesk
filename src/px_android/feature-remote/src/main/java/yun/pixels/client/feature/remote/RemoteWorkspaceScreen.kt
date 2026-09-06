package yun.pixels.client.feature.remote

import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.MotionEvent
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Close
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface as MaterialSurface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import yun.pixels.client.core.domain.session.RemoteSessionSnapshot
import yun.pixels.client.core.domain.session.RemoteSessionStatus
import yun.pixels.client.core.domain.session.PointerAction

@Composable
fun RemoteWorkspaceScreen(
    snapshot: RemoteSessionSnapshot,
    onSurfaceAvailable: (Surface) -> Unit,
    onSurfaceDestroyed: () -> Unit,
    onPointer: (PointerAction, Float, Float) -> Unit,
    onEndSession: () -> Unit,
) {
    BackHandler(onBack = onEndSession)
    val latestSurfaceAvailable by rememberUpdatedState(onSurfaceAvailable)
    val latestSurfaceDestroyed by rememberUpdatedState(onSurfaceDestroyed)
    val latestPointer by rememberUpdatedState(onPointer)
    val surfaceCallback = remember {
        object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                if (holder.surface.isValid) latestSurfaceAvailable(holder.surface)
            }

            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) = Unit

            override fun surfaceDestroyed(holder: SurfaceHolder) = latestSurfaceDestroyed()
        }
    }

    Box(modifier = Modifier.fillMaxSize().background(Color.Black)) {
        AndroidView(
            factory = { context ->
                SurfaceView(context).also { view ->
                    view.holder.addCallback(surfaceCallback)
                    view.setOnTouchListener { touchedView, event ->
                        if (touchedView.width <= 0 || touchedView.height <= 0) return@setOnTouchListener true
                        val action = when (event.actionMasked) {
                            MotionEvent.ACTION_DOWN -> PointerAction.Down
                            MotionEvent.ACTION_MOVE -> PointerAction.Move
                            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> PointerAction.Up
                            else -> return@setOnTouchListener true
                        }
                        latestPointer(
                            action,
                            (event.x / touchedView.width).coerceIn(0f, 1f),
                            (event.y / touchedView.height).coerceIn(0f, 1f),
                        )
                        if (event.actionMasked == MotionEvent.ACTION_UP) touchedView.performClick()
                        true
                    }
                }
            },
            modifier = Modifier.fillMaxSize(),
            onRelease = { view ->
                view.setOnTouchListener(null)
                view.holder.removeCallback(surfaceCallback)
            },
        )
        RemoteTopBar(snapshot = snapshot, onEndSession = onEndSession)
        RemoteStatus(snapshot.status)
    }
}

@Composable
private fun RemoteTopBar(snapshot: RemoteSessionSnapshot, onEndSession: () -> Unit) {
    val status = snapshot.status
    val title = when (status) {
        RemoteSessionStatus.Idle -> "Pixels"
        is RemoteSessionStatus.Starting -> status.request.target.displayName
        is RemoteSessionStatus.Connected -> status.request.target.displayName
        is RemoteSessionStatus.Reconnecting -> status.request.target.displayName
        is RemoteSessionStatus.Stopping -> status.request.target.displayName
        is RemoteSessionStatus.Failed -> status.request.target.displayName
    }
    MaterialSurface(
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.86f),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column {
                Text(text = title, style = MaterialTheme.typography.titleMedium)
                if (status is RemoteSessionStatus.Connected) {
                    Text(
                        text = stringResource(
                            R.string.remote_statistics,
                            snapshot.statistics.framesPerSecond,
                            snapshot.statistics.latencyMillis,
                            snapshot.statistics.bitrateKbps,
                        ),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
            FilledTonalIconButton(onClick = onEndSession) {
                Icon(Icons.Outlined.Close, contentDescription = stringResource(R.string.remote_exit))
            }
        }
    }
}

@Composable
private fun BoxScope.RemoteStatus(status: RemoteSessionStatus) {
    val message = when (status) {
        RemoteSessionStatus.Idle -> R.string.remote_connecting
        is RemoteSessionStatus.Starting -> R.string.remote_connecting
        is RemoteSessionStatus.Reconnecting -> R.string.remote_reconnecting
        is RemoteSessionStatus.Stopping -> R.string.remote_stopping
        is RemoteSessionStatus.Failed -> R.string.remote_failed
        is RemoteSessionStatus.Connected -> null
    }
    if (message != null) {
        Column(
            modifier = Modifier.align(Alignment.Center),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            if (status !is RemoteSessionStatus.Failed) CircularProgressIndicator()
            Text(text = stringResource(message), color = Color.White)
        }
    }
}
