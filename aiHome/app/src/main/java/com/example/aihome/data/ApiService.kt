package com.example.aihome.data

import com.google.gson.Gson
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.MediaType.Companion.toMediaType
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import java.util.concurrent.TimeUnit

class ApiService {
    private val client = OkHttpClient.Builder()
        .connectTimeout(5, TimeUnit.SECONDS)
        .readTimeout(10, TimeUnit.SECONDS)
        .writeTimeout(10, TimeUnit.SECONDS)
        .build()

    private val gson = Gson()
    private val jsonMediaType = "application/json; charset=utf-8".toMediaType()

    // ── Bridge: status ──
    suspend fun getStatus(baseUrl: String): Result<StatusResponse> = withContext(Dispatchers.IO) {
        try {
            val request = Request.Builder()
                .url("$baseUrl/status")
                .get()
                .build()
            val response = client.newCall(request).execute()
            if (response.isSuccessful) {
                val body = response.body?.string() ?: "{}"
                val status = gson.fromJson(body, StatusResponse::class.java)
                Result.success(status)
            } else {
                Result.failure(Exception("Server error: ${response.code}"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    // ── Bridge: send command ──
    suspend fun sendCommand(
        baseUrl: String,
        tool: String,
        args: Map<String, String>
    ): Result<CommandResponse> = withContext(Dispatchers.IO) {
        try {
            val commandRequest = CommandRequest(tool, args)
            val jsonBody = gson.toJson(commandRequest)
            val requestBody = jsonBody.toRequestBody(jsonMediaType)

            val request = Request.Builder()
                .url("$baseUrl/command")
                .post(requestBody)
                .build()

            val response = client.newCall(request).execute()
            if (response.isSuccessful) {
                val body = response.body?.string() ?: "{}"
                val cmdResponse = gson.fromJson(body, CommandResponse::class.java)
                Result.success(cmdResponse)
            } else {
                Result.failure(Exception("Server error: ${response.code}"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    // ── Bridge: Schedules ──
    suspend fun getSchedules(baseUrl: String): Result<List<Schedule>> = withContext(Dispatchers.IO) {
        try {
            val request = Request.Builder()
                .url("$baseUrl/schedules")
                .get()
                .build()
            val response = client.newCall(request).execute()
            if (response.isSuccessful) {
                val body = response.body?.string() ?: "{}"
                val parsed = gson.fromJson(body, ScheduleListResponse::class.java)
                if (parsed.success) {
                    Result.success(parsed.schedules)
                } else {
                    Result.failure(Exception("Failed to load schedules"))
                }
            } else {
                Result.failure(Exception("Server error: ${response.code}"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    suspend fun addSchedule(baseUrl: String, tool: String, args: Map<String, String>, time: String): Result<Boolean> = withContext(Dispatchers.IO) {
        try {
            val req = ScheduleAddRequest(tool, args, time)
            val requestBody = gson.toJson(req).toRequestBody(jsonMediaType)
            val request = Request.Builder()
                .url("$baseUrl/schedules/add")
                .post(requestBody)
                .build()
            val response = client.newCall(request).execute()
            
            if (response.isSuccessful) {
                val body = response.body?.string() ?: "{}"
                val parsed = gson.fromJson(body, ScheduleActionResponse::class.java)
                if (parsed.success) Result.success(true) else Result.failure(Exception(parsed.error ?: "Error adding"))
            } else {
                Result.failure(Exception("Server error: ${response.code}"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    suspend fun deleteSchedule(baseUrl: String, id: String): Result<Boolean> = withContext(Dispatchers.IO) {
        try {
            val req = ScheduleDeleteRequest(id)
            val requestBody = gson.toJson(req).toRequestBody(jsonMediaType)
            val request = Request.Builder()
                .url("$baseUrl/schedules/delete")
                .post(requestBody)
                .build()
            val response = client.newCall(request).execute()
            
            if (response.isSuccessful) {
                val body = response.body?.string() ?: "{}"
                val parsed = gson.fromJson(body, ScheduleActionResponse::class.java)
                if (parsed.success) Result.success(true) else Result.failure(Exception(parsed.error ?: "Error deleting"))
            } else {
                Result.failure(Exception("Server error: ${response.code}"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    // ── Bridge: DHT11 sensor data ──
    suspend fun getSensorData(baseUrl: String): Result<SensorDataResponse> = withContext(Dispatchers.IO) {
        try {
            val request = Request.Builder()
                .url("$baseUrl/sensors")
                .get()
                .build()
            val response = client.newCall(request).execute()
            if (response.isSuccessful) {
                val body = response.body?.string() ?: "{}"
                val parsed = gson.fromJson(body, SensorDataResponse::class.java)
                Result.success(parsed)
            } else {
                Result.failure(Exception("Sensor API error: ${response.code}"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    // ── Bridge: devices status (đèn, quạt) ──
    suspend fun getDevicesStatus(baseUrl: String): Result<DevicesStatusResponse> = withContext(Dispatchers.IO) {
        try {
            val request = Request.Builder()
                .url("$baseUrl/devices_status")
                .get()
                .build()
            val response = client.newCall(request).execute()
            if (response.isSuccessful) {
                val body = response.body?.string() ?: "{}"
                val parsed = gson.fromJson(body, DevicesStatusResponse::class.java)
                Result.success(parsed)
            } else {
                Result.failure(Exception("Devices status error: ${response.code}"))
            }
        } catch (e: Exception) {
            Result.failure(e)
        }
    }

    // ── Bridge: check connection ──
    suspend fun checkConnection(baseUrl: String): Boolean = withContext(Dispatchers.IO) {
        try {
            val request = Request.Builder()
                .url("$baseUrl/status")
                .get()
                .build()
            val response = client.newCall(request).execute()
            response.isSuccessful
        } catch (e: Exception) {
            false
        }
    }
}
