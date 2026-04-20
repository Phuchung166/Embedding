package com.example.aihome.ui.theme

import android.app.Activity
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.sp
import androidx.core.view.WindowCompat

// Clean Light / Standard Smart Home Colors
val PrimaryBlue = Color(0xFF007AFF) // Classic iOS/Google Home Accent
val PrimaryBlueDark = Color(0xFF005BB5)
val BackgroundLight = Color(0xFFF2F2F7) // Light gray matching Apple Home / Settings
val CardWhite = Color(0xFFFFFFFF)
val TextDark = Color(0xFF1C1C1E)
val TextGray = Color(0xFF8E8E93)
val TextGrayLight = Color(0xFFC7C7CC)

// Device status colors
val DeviceOnOrange = Color(0xFFF59E0B) // Amber for lights
val DeviceOnBlue = Color(0xFF3B82F6) // Blue for things like fans/clotheslines
val DeviceOffGray = Color(0xFFE5E5EA)

// Status colors
val AlertRed = Color(0xFFFF3B30)
val SuccessGreen = Color(0xFF34C759)

private val LightColorScheme = lightColorScheme(
    primary = PrimaryBlue,
    onPrimary = Color.White,
    secondary = PrimaryBlueDark,
    onSecondary = Color.White,
    tertiary = DeviceOnOrange,
    background = BackgroundLight,
    onBackground = TextDark,
    surface = CardWhite,
    onSurface = TextDark,
    surfaceVariant = Color(0xFFF2F2F7),
    onSurfaceVariant = TextGray,
    error = AlertRed,
    onError = Color.White,
    outline = Color(0xFFE5E5EA)
)

val AppTypography = Typography(
    headlineLarge = TextStyle(
        fontWeight = FontWeight.Bold,
        fontSize = 32.sp,
        letterSpacing = (-1).sp,
        color = TextDark
    ),
    headlineMedium = TextStyle(
        fontWeight = FontWeight.Bold,
        fontSize = 24.sp,
        letterSpacing = (-0.5).sp,
        color = TextDark
    ),
    titleLarge = TextStyle(
        fontWeight = FontWeight.SemiBold,
        fontSize = 20.sp,
        letterSpacing = 0.sp,
        color = TextDark
    ),
    titleMedium = TextStyle(
        fontWeight = FontWeight.Medium,
        fontSize = 16.sp,
        letterSpacing = 0.15.sp,
        color = TextDark
    ),
    bodyLarge = TextStyle(
        fontWeight = FontWeight.Normal,
        fontSize = 16.sp,
        letterSpacing = 0.5.sp,
        color = TextDark
    ),
    bodyMedium = TextStyle(
        fontWeight = FontWeight.Normal,
        fontSize = 14.sp,
        letterSpacing = 0.25.sp,
        color = TextGray
    ),
    labelLarge = TextStyle(
        fontWeight = FontWeight.SemiBold,
        fontSize = 14.sp,
        letterSpacing = 0.1.sp,
        color = TextDark
    ),
    labelMedium = TextStyle(
        fontWeight = FontWeight.Medium,
        fontSize = 12.sp,
        letterSpacing = 0.5.sp,
        color = TextGray
    )
)

@Composable
fun AIHomeTheme(content: @Composable () -> Unit) {
    val view = LocalView.current
    if (!view.isInEditMode) {
        SideEffect {
            val window = (view.context as Activity).window
            window.statusBarColor = BackgroundLight.toArgb()
            window.navigationBarColor = BackgroundLight.toArgb()
            // Make icons dark on light background
            WindowCompat.getInsetsController(window, view).isAppearanceLightStatusBars = false
        }
    }

    MaterialTheme(
        colorScheme = LightColorScheme,
        typography = AppTypography,
        content = content
    )
}
