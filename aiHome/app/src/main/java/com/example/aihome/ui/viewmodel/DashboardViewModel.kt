package com.example.aihome.ui.viewmodel

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.example.aihome.data.ApiService
import com.example.aihome.data.PrefsManager
import com.example.aihome.data.Schedule
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class DashboardUiState(
    // Connection
    val isOnline: Boolean = false,
    val serverIp: String = "",
    val serverPort: String = "8080",

    // Environment (from DHT11 sensor via bridge)
    val temperature: Float = 0f,
    val humidity: Int = 0,
    val sensorError: Boolean = false, // true nếu DHT11 đọc lỗi

    // Device states
    val fanOn: Boolean = false,
    val livingRoomLightOn: Boolean = false,
    val bedroomLightOn: Boolean = false,
    val kitchenLightOn: Boolean = false,
    val clotheslineExtended: Boolean = false,

    // Schedules
    val schedules: List<Schedule> = emptyList(),

    // Loading states
    val loadingFan: Boolean = false,
    val loadingLivingRoom: Boolean = false,
    val loadingBedroom: Boolean = false,
    val loadingKitchen: Boolean = false,
    val loadingClothesline: Boolean = false,
    val loadingAllDevices: Boolean = false,
    val loadingSensors: Boolean = false,
    val loadingSchedules: Boolean = false,

    // Error
    val lastError: String? = null
)

class DashboardViewModel(application: Application) : AndroidViewModel(application) {

    private val apiService = ApiService()
    private val prefsManager = PrefsManager(application)

    private val _uiState = MutableStateFlow(DashboardUiState())
    val uiState: StateFlow<DashboardUiState> = _uiState.asStateFlow()

    private var pollingJob: Job? = null

    private val baseUrl: String
        get() = "http://${_uiState.value.serverIp.trim()}:${_uiState.value.serverPort.trim()}"

    init {
        loadConfig()
    }

    private fun loadConfig() {
        val ip = prefsManager.getIp()
        val port = prefsManager.getPort()
        _uiState.update { it.copy(serverIp = ip, serverPort = port) }
        if (ip.isNotBlank()) {
            startPolling()
        }
    }

    fun updateConfig(ip: String, port: String) {
        prefsManager.saveConfig(ip, port)
        _uiState.update { it.copy(serverIp = ip, serverPort = port) }
        startPolling()
    }

    fun startPolling() {
        pollingJob?.cancel()
        pollingJob = viewModelScope.launch {
            var ticks = 0
            while (true) {
                fastRefresh()
                if (ticks % 5 == 0) { // Mỗi 15 giây
                    slowRefresh()
                }
                ticks++
                delay(3000) // Fast poll mỗi 3 giây
            }
        }
    }

    fun stopPolling() {
        pollingJob?.cancel()
    }

    private suspend fun fastRefresh() {
        // 1. Check bridge connection
        val statusResult = apiService.getStatus(baseUrl)
        statusResult.onSuccess {
            _uiState.update { s -> s.copy(isOnline = true, lastError = null) }
        }.onFailure { e ->
            _uiState.update { s -> s.copy(isOnline = false, lastError = e.message) }
        }

        // 2. Read actual device states from bridge cache (fast)
        fetchDeviceStatus()
    }

    private suspend fun slowRefresh() {
        // 1. Read DHT11 sensor from bridge (proxied to R4)
        fetchSensorData()
        // 2. Get schedules
        fetchSchedules()
    }

    private suspend fun fetchSensorData() {
        val res = apiService.getSensorData(baseUrl)
        res.onSuccess { s ->
            if (s.success) {
                _uiState.update { it.copy(temperature = s.temperature, humidity = s.humidity.toInt(), sensorError = false) }
            } else {
                _uiState.update { it.copy(sensorError = true) }
            }
        }.onFailure {
            _uiState.update { it.copy(sensorError = true) }
        }
    }

    private suspend fun fetchDeviceStatus() {
        val res = apiService.getDevicesStatus(baseUrl)
        res.onSuccess { d ->
            if (d.success) {
                _uiState.update { it.copy(
                    fanOn            = d.fan == "on",
                    livingRoomLightOn = d.livingRoomLight == "on",
                    bedroomLightOn   = d.bedroomLight == "on",
                    kitchenLightOn   = d.kitchenLight == "on",
                    clotheslineExtended = d.clothesline == "extended"
                )}
            }
        }
    }

    private suspend fun fetchSchedules() {
        val schRes = apiService.getSchedules(baseUrl)
        schRes.onSuccess { list ->
            _uiState.update { it.copy(schedules = list) }
        }
    }

    fun addSchedule(time: String, tool: String, actionArgs: Map<String, String>) {
        viewModelScope.launch {
            _uiState.update { it.copy(loadingSchedules = true) }
            val res = apiService.addSchedule(baseUrl, tool, actionArgs, time)
            _uiState.update { it.copy(loadingSchedules = false) }
            res.onSuccess {
                fetchSchedules() // reload
            }.onFailure { e ->
                _uiState.update { it.copy(lastError = e.message) }
            }
        }
    }

    fun deleteSchedule(id: String) {
        viewModelScope.launch {
            _uiState.update { it.copy(loadingSchedules = true) }
            val res = apiService.deleteSchedule(baseUrl, id)
            _uiState.update { it.copy(loadingSchedules = false) }
            res.onSuccess {
                fetchSchedules() // reload
            }.onFailure { e ->
                _uiState.update { it.copy(lastError = e.message) }
            }
        }
    }

    // ── Device Controls ──

    fun toggleFan() {
        val newState = if (_uiState.value.fanOn) "off" else "on"
        sendDeviceCommand("fan_control", mapOf("state" to newState), "loadingFan") { success ->
            if (success) _uiState.update { it.copy(fanOn = !it.fanOn) }
        }
    }

    fun toggleLivingRoomLight() {
        val newState = if (_uiState.value.livingRoomLightOn) "off" else "on"
        sendDeviceCommand("living_room_lights_control", mapOf("state" to newState), "loadingLivingRoom") { success ->
            if (success) _uiState.update { it.copy(livingRoomLightOn = !it.livingRoomLightOn) }
        }
    }

    fun toggleBedroomLight() {
        val newState = if (_uiState.value.bedroomLightOn) "off" else "on"
        sendDeviceCommand("bedroom_lights_control", mapOf("state" to newState), "loadingBedroom") { success ->
            if (success) _uiState.update { it.copy(bedroomLightOn = !it.bedroomLightOn) }
        }
    }

    fun toggleKitchenLight() {
        val newState = if (_uiState.value.kitchenLightOn) "off" else "on"
        sendDeviceCommand("kitchen_lights_control", mapOf("state" to newState), "loadingKitchen") { success ->
            if (success) _uiState.update { it.copy(kitchenLightOn = !it.kitchenLightOn) }
        }
    }

    fun controlClothesline(extend: Boolean) {
        val action = if (extend) "extend" else "retract"
        sendDeviceCommand("clothesline_control", mapOf("action" to action), "loadingClothesline") { success ->
            if (success) _uiState.update { it.copy(clotheslineExtended = extend) }
        }
    }

    fun allDevices(on: Boolean) {
        val state = if (on) "on" else "off"
        _uiState.update { it.copy(loadingAllDevices = true) }
        viewModelScope.launch {
            var allOk = true
            val devices = listOf(
                "fan_control",
                "living_room_lights_control",
                "bedroom_lights_control",
                "kitchen_lights_control"
            )
            for (tool in devices) {
                val result = apiService.sendCommand(baseUrl, tool, mapOf("state" to state))
                if (result.isFailure) allOk = false
            }
            _uiState.update { it.copy(loadingAllDevices = false) }
            if (allOk) {
                _uiState.update {
                    it.copy(
                        fanOn = on,
                        livingRoomLightOn = on,
                        bedroomLightOn = on,
                        kitchenLightOn = on
                    )
                }
            }
        }
    }

    private fun sendDeviceCommand(
        tool: String,
        args: Map<String, String>,
        loadingField: String,
        onResult: (Boolean) -> Unit
    ) {
        viewModelScope.launch {
            setLoading(loadingField, true)
            val result = apiService.sendCommand(baseUrl, tool, args)
            setLoading(loadingField, false)
            result.onSuccess {
                onResult(true)
            }.onFailure { e ->
                _uiState.update { it.copy(lastError = e.message) }
                onResult(false)
            }
        }
    }

    private fun setLoading(field: String, loading: Boolean) {
        _uiState.update {
            when (field) {
                "loadingFan" -> it.copy(loadingFan = loading)
                "loadingLivingRoom" -> it.copy(loadingLivingRoom = loading)
                "loadingBedroom" -> it.copy(loadingBedroom = loading)
                "loadingKitchen" -> it.copy(loadingKitchen = loading)
                "loadingClothesline" -> it.copy(loadingClothesline = loading)
                "loadingAllDevices" -> it.copy(loadingAllDevices = loading)
                else -> it
            }
        }
    }

    fun manualRefresh() {
        viewModelScope.launch {
            _uiState.update { it.copy(loadingSensors = true) }
            fastRefresh()
            slowRefresh()
            _uiState.update { it.copy(loadingSensors = false) }
        }
    }

    override fun onCleared() {
        super.onCleared()
        stopPolling()
    }
}
