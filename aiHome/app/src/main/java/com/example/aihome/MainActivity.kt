package com.example.aihome

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.runtime.*
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.example.aihome.data.PrefsManager
import com.example.aihome.ui.screens.DashboardScreen
import com.example.aihome.ui.screens.SetupScreen
import com.example.aihome.ui.theme.AIHomeTheme
import com.example.aihome.ui.viewmodel.DashboardViewModel

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        setContent {
            AIHomeTheme {
                val prefsManager = remember { PrefsManager(this@MainActivity) }
                val viewModel: DashboardViewModel = viewModel()
                val uiState by viewModel.uiState.collectAsStateWithLifecycle()

                var showSetup by remember {
                    mutableStateOf(!prefsManager.isConfigured())
                }

                if (showSetup) {
                    SetupScreen(
                        initialIp = prefsManager.getIp(),
                        initialPort = prefsManager.getPort(),
                        onConnect = { ip, port ->
                            viewModel.updateConfig(ip, port)
                            showSetup = false
                        }
                    )
                } else {
                    DashboardScreen(
                        uiState = uiState,
                        onToggleFan = viewModel::toggleFan,
                        onToggleLivingRoom = viewModel::toggleLivingRoomLight,
                        onToggleBedroom = viewModel::toggleBedroomLight,
                        onToggleKitchen = viewModel::toggleKitchenLight,
                        onClothesline = viewModel::controlClothesline,
                        onAllDevices = viewModel::allDevices,
                        onRefresh = viewModel::manualRefresh,
                        onChangeIp = { showSetup = true },
                        onAddSchedule = viewModel::addSchedule,
                        onDeleteSchedule = viewModel::deleteSchedule
                    )
                }
            }
        }
    }
}
