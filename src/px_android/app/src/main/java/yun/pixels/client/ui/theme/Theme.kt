package yun.pixels.client.ui.theme

import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable

private val PixelsDarkColorScheme = darkColorScheme(
    primary = PixelsDarkPrimary,
    onPrimary = PixelsDarkOnPrimary,
    secondary = PixelsDarkSecondary,
    tertiary = PixelsDarkWarning,
    background = PixelsDarkBackground,
    onBackground = PixelsDarkText,
    surface = PixelsDarkSurface,
    onSurface = PixelsDarkText,
    surfaceVariant = PixelsDarkSurfaceRaised,
    onSurfaceVariant = PixelsDarkTextSecondary,
    outline = PixelsDarkOutline,
    error = PixelsDarkError,
)

private val PixelsLightColorScheme = lightColorScheme(
    primary = PixelsLightPrimary,
    onPrimary = PixelsLightOnPrimary,
    secondary = PixelsLightSecondary,
    tertiary = PixelsLightWarning,
    background = PixelsLightBackground,
    onBackground = PixelsLightText,
    surface = PixelsLightSurface,
    onSurface = PixelsLightText,
    surfaceVariant = PixelsLightSurfaceRaised,
    onSurfaceVariant = PixelsLightTextSecondary,
    outline = PixelsLightOutline,
    error = PixelsLightError,
)

@Composable
fun PixelsTheme(
    darkTheme: Boolean = isSystemInDarkTheme(),
    content: @Composable () -> Unit,
) {
    MaterialTheme(
        colorScheme = if (darkTheme) PixelsDarkColorScheme else PixelsLightColorScheme,
        typography = PixelsTypography,
        content = content,
    )
}
