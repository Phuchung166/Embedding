package com.example.aihome.data

import android.content.Context
import android.content.SharedPreferences

class PrefsManager(context: Context) {
    private val prefs: SharedPreferences =
        context.getSharedPreferences("aihome_prefs", Context.MODE_PRIVATE)

    companion object {
        private const val KEY_IP = "server_ip"
        private const val KEY_PORT = "server_port"
    }

    fun saveConfig(ip: String, port: String) {
        prefs.edit()
            .putString(KEY_IP, ip)
            .putString(KEY_PORT, port)
            .apply()
    }

    fun getIp(): String = prefs.getString(KEY_IP, "") ?: ""
    fun getPort(): String = prefs.getString(KEY_PORT, "8080") ?: "8080"
    fun isConfigured(): Boolean = getIp().isNotBlank()

    fun clearConfig() {
        prefs.edit().clear().apply()
    }
}
