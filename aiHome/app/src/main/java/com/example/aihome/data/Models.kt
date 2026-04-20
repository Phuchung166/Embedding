package com.example.aihome.data

import com.google.gson.annotations.SerializedName

data class StatusResponse(
    @SerializedName("ws_connected") val wsConnected: Boolean = false,
    @SerializedName("pending_commands") val pendingCommands: Int = 0
)

data class CommandRequest(
    @SerializedName("tool") val tool: String,
    @SerializedName("args") val args: Map<String, String>
)

data class CommandResponse(
    @SerializedName("device") val device: String? = null,
    @SerializedName("state") val state: String? = null,
    @SerializedName("action") val action: String? = null,
    @SerializedName("success") val success: Boolean? = null,
    @SerializedName("error") val error: String? = null
)

// --- Schedules API ---
data class ScheduleListResponse(
    @SerializedName("success") val success: Boolean = false,
    @SerializedName("schedules") val schedules: List<Schedule> = emptyList()
)

data class ScheduleActionResponse(
    @SerializedName("success") val success: Boolean = false,
    @SerializedName("error") val error: String? = null
)

data class Schedule(
    @SerializedName("id") val id: String = "",
    @SerializedName("tool") val tool: String = "",
    @SerializedName("args") val args: Map<String, String> = emptyMap(),
    @SerializedName("time") val time: String = "", // Format HH:MM
    @SerializedName("executed_today") val executedToday: Boolean = false
)

data class ScheduleAddRequest(
    @SerializedName("tool") val tool: String,
    @SerializedName("args") val args: Map<String, String>,
    @SerializedName("time") val time: String
)

data class ScheduleDeleteRequest(
    @SerializedName("id") val id: String
)

// --- DHT11 Sensor Data ---
data class SensorDataResponse(
    @SerializedName("success") val success: Boolean = false,
    @SerializedName("temperature") val temperature: Float = 0f,
    @SerializedName("humidity") val humidity: Float = 0f,
    @SerializedName("error") val error: String? = null
)

// --- Devices Status ---
data class DevicesStatusResponse(
    @SerializedName("success") val success: Boolean = false,
    @SerializedName("fan") val fan: String = "off",
    @SerializedName("living_room_light") val livingRoomLight: String = "off",
    @SerializedName("bedroom_light") val bedroomLight: String = "off",
    @SerializedName("kitchen_light") val kitchenLight: String = "off",
    @SerializedName("clothesline") val clothesline: String = "retracted",
    @SerializedName("error") val error: String? = null
)
