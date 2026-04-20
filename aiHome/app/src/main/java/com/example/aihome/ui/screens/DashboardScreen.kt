package com.example.aihome.ui.screens

import androidx.compose.animation.*
import androidx.compose.animation.animateColorAsState
import androidx.compose.foundation.*
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.*
import androidx.compose.material.icons.outlined.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.example.aihome.data.Schedule
import com.example.aihome.ui.theme.*
import com.example.aihome.ui.viewmodel.DashboardUiState

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DashboardScreen(
    uiState: DashboardUiState,
    onToggleFan: () -> Unit,
    onToggleLivingRoom: () -> Unit,
    onToggleBedroom: () -> Unit,
    onToggleKitchen: () -> Unit,
    onClothesline: (extend: Boolean) -> Unit,
    onAllDevices: (on: Boolean) -> Unit,
    onRefresh: () -> Unit,
    onChangeIp: () -> Unit,
    onAddSchedule: ((String, String, Map<String, String>) -> Unit)? = null, // (time, tool, args)
    onDeleteSchedule: ((String) -> Unit)? = null
) {
    val scrollState = rememberScrollState()
    var showScheduleDialog by remember { mutableStateOf(false) }

    if (showScheduleDialog && onAddSchedule != null) {
        AddScheduleDialog(
            onDismiss = { showScheduleDialog = false },
            onConfirm = { time, tool, state ->
                onAddSchedule(time, tool, mapOf("state" to state))
                showScheduleDialog = false
            }
        )
    }

    Scaffold(
        containerColor = BackgroundLight,
        topBar = {
            TopAppBar(
                title = {
                    Column {
                        Text("Nhà của tôi", fontWeight = FontWeight.Bold, fontSize = 28.sp, color = TextDark)
                        Text("${uiState.serverIp}:${uiState.serverPort}", fontSize = 12.sp, color = TextGray)
                    }
                },
                actions = {
                    ConnectionBadge(isOnline = uiState.isOnline)
                    IconButton(onClick = onChangeIp) {
                        Icon(Icons.Default.Settings, "Cài đặt", tint = TextDark)
                    }
                    IconButton(onClick = onRefresh) {
                        if (uiState.loadingSensors || uiState.loadingSchedules) {
                            CircularProgressIndicator(modifier = Modifier.size(20.dp), color = PrimaryBlue, strokeWidth = 2.dp)
                        } else {
                            Icon(Icons.Default.Refresh, "Làm mới", tint = TextDark)
                        }
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = BackgroundLight)
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
                .verticalScroll(scrollState)
                .padding(horizontal = 20.dp, vertical = 8.dp),
            verticalArrangement = Arrangement.spacedBy(24.dp)
        ) {
            // Environment Widget
            EnvironmentWidget(temp = uiState.temperature, hum = uiState.humidity, sensorError = uiState.sensorError)

            AnimatedVisibility(visible = uiState.lastError != null) {
                uiState.lastError?.let { errorMsg ->
                    ErrorCard(error = errorMsg)
                }
            }

            // Quick Actions
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                HomeKitActionButton(
                    modifier = Modifier.weight(1f),
                    label = "Bật hết",
                    icon = Icons.Default.FlashOn,
                    isLoading = uiState.loadingAllDevices,
                    onClick = { onAllDevices(true) }
                )
                HomeKitActionButton(
                    modifier = Modifier.weight(1f),
                    label = "Tắt hết",
                    icon = Icons.Default.FlashOff,
                    isLoading = uiState.loadingAllDevices,
                    onClick = { onAllDevices(false) }
                )
            }

            // Scenes / Devices
            SectionTitle("Phòng & Lịch trình")
            Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    HomeKitDeviceToggle(
                        modifier = Modifier.weight(1f),
                        icon = if (uiState.livingRoomLightOn) Icons.Default.Lightbulb else Icons.Outlined.Lightbulb,
                        label = "Phòng Khách",
                        stateText = if (uiState.livingRoomLightOn) "Đang bật" else "Đang tắt",
                        isOn = uiState.livingRoomLightOn,
                        isLoading = uiState.loadingLivingRoom,
                        activeColor = DeviceOnOrange,
                        onToggle = onToggleLivingRoom
                    )
                    HomeKitDeviceToggle(
                        modifier = Modifier.weight(1f),
                        icon = if (uiState.bedroomLightOn) Icons.Default.Bed else Icons.Outlined.Bed,
                        label = "Phòng Ngủ",
                        stateText = if (uiState.bedroomLightOn) "Đang bật" else "Đang tắt",
                        isOn = uiState.bedroomLightOn,
                        isLoading = uiState.loadingBedroom,
                        activeColor = DeviceOnOrange,
                        onToggle = onToggleBedroom
                    )
                }

                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    HomeKitDeviceToggle(
                        modifier = Modifier.weight(1f),
                        icon = if (uiState.kitchenLightOn) Icons.Default.Blender else Icons.Outlined.Blender,
                        label = "Phòng Bếp",
                        stateText = if (uiState.kitchenLightOn) "Đang bật" else "Đang tắt",
                        isOn = uiState.kitchenLightOn,
                        isLoading = uiState.loadingKitchen,
                        activeColor = DeviceOnOrange,
                        onToggle = onToggleKitchen
                    )
                    HomeKitDeviceToggle(
                        modifier = Modifier.weight(1f),
                        icon = if (uiState.fanOn) Icons.Default.Air else Icons.Outlined.Air,
                        label = "Quạt Máy",
                        stateText = if (uiState.fanOn) "Đang bật" else "Đang tắt",
                        isOn = uiState.fanOn,
                        isLoading = uiState.loadingFan,
                        activeColor = DeviceOnBlue,
                        onToggle = onToggleFan
                    )
                }
            }

            // Clothesline (Phơi đồ)
            SectionTitle("Ngoài sân")
            HomeKitClotheslineCard(
                isExtended = uiState.clotheslineExtended,
                isLoading = uiState.loadingClothesline,
                onControl = onClothesline
            )

            // Schedules
            Row(
                modifier = Modifier.fillMaxWidth(), 
                horizontalArrangement = Arrangement.SpaceBetween, 
                verticalAlignment = Alignment.CenterVertically
            ) {
                SectionTitle("Lịch tự động (Hẹn giờ)")
                IconButton(onClick = { showScheduleDialog = true }) {
                    Icon(Icons.Default.AddCircle, contentDescription = "Thêm lịch", tint = PrimaryBlue)
                }
            }

            if (uiState.schedules.isEmpty()) {
                Text("Chưa có lịch hẹn nào.", color = TextGray, fontSize = 14.sp)
            } else {
                uiState.schedules.forEach { schedule ->
                    ScheduleItemCard(
                        schedule = schedule,
                        onDelete = { onDeleteSchedule?.invoke(schedule.id) }
                    )
                }
            }

            Spacer(modifier = Modifier.height(32.dp))
        }
    }
}

// ==========================================
// CLEAN HOMEKIT-STYLE COMPONENTS
// ==========================================

@Composable
fun SectionTitle(title: String) {
    Text(
        text = title,
        style = MaterialTheme.typography.titleLarge,
        color = TextDark,
        modifier = Modifier.padding(bottom = 0.dp, top = 8.dp)
    )
}

@Composable
fun EnvironmentWidget(temp: Float, hum: Int, sensorError: Boolean) {
    val tempText  = if (sensorError) "--" else "${temp}°C"
    val humText   = if (sensorError) "--" else "${hum}%"
    val (statusText, statusColor) = when {
        sensorError -> "Lỗi cảm biến" to AlertRed
        hum <= 40   -> "Khô" to DeviceOnOrange
        hum <= 70   -> "Dễ chịu" to SuccessGreen
        else        -> "Ẩm" to DeviceOnBlue
    }

    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(24.dp),
        colors = CardDefaults.cardColors(containerColor = CardWhite),
        elevation = CardDefaults.cardElevation(defaultElevation = 0.dp)
    ) {
        Column(modifier = Modifier.padding(20.dp)) {
            Text("Cảm biến DHT11", fontWeight = FontWeight.SemiBold, fontSize = 16.sp, color = TextDark)
            Spacer(modifier = Modifier.height(16.dp))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                EnvStatItem(icon = Icons.Default.Thermostat, value = tempText,   label = "Nhiệt độ",  iconTint = DeviceOnOrange)
                EnvStatItem(icon = Icons.Default.WaterDrop,  value = humText,    label = "Độ ẩm",     iconTint = DeviceOnBlue)
                EnvStatItem(icon = Icons.Default.Air,        value = statusText, label = "Trạng thái", iconTint = statusColor)
            }
        }
    }
}

@Composable
fun EnvStatItem(icon: ImageVector, value: String, label: String, iconTint: Color) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Box(
            modifier = Modifier.size(40.dp).clip(CircleShape).background(iconTint.copy(alpha = 0.1f)),
            contentAlignment = Alignment.Center
        ) {
            Icon(icon, contentDescription = null, tint = iconTint, modifier = Modifier.size(20.dp))
        }
        Spacer(modifier = Modifier.height(8.dp))
        Text(text = value, fontSize = 16.sp, fontWeight = FontWeight.Bold, color = TextDark)
        Text(text = label, fontSize = 12.sp, color = TextGray)
    }
}

@Composable
fun HomeKitDeviceToggle(
    modifier: Modifier = Modifier,
    icon: ImageVector,
    label: String,
    stateText: String,
    isOn: Boolean,
    isLoading: Boolean,
    activeColor: Color,
    onToggle: () -> Unit
) {
    val containerColor by animateColorAsState(targetValue = if (isOn) activeColor else CardWhite)
    val contentColor by animateColorAsState(targetValue = if (isOn) Color.White else TextDark)
    val subTextColor by animateColorAsState(targetValue = if (isOn) Color.White.copy(alpha = 0.8f) else TextGray)
    val iconColor by animateColorAsState(targetValue = if (isOn) activeColor else TextGrayLight)
    val iconBgColor by animateColorAsState(targetValue = if (isOn) Color.White else DeviceOffGray)

    Card(
        onClick = { if (!isLoading) onToggle() },
        modifier = modifier.height(130.dp),
        shape = RoundedCornerShape(24.dp),
        colors = CardDefaults.cardColors(containerColor = containerColor)
    ) {
        Column(
            modifier = Modifier.fillMaxSize().padding(16.dp),
            verticalArrangement = Arrangement.SpaceBetween,
            horizontalAlignment = Alignment.Start
        ) {
            Box(
                modifier = Modifier
                    .size(40.dp)
                    .clip(CircleShape)
                    .background(iconBgColor),
                contentAlignment = Alignment.Center
            ) {
                if (isLoading) {
                    CircularProgressIndicator(modifier = Modifier.size(20.dp), color = iconColor, strokeWidth = 2.dp)
                } else {
                    Icon(icon, contentDescription = null, tint = iconColor, modifier = Modifier.size(24.dp))
                }
            }

            Column {
                Text(text = label, fontSize = 15.sp, fontWeight = FontWeight.SemiBold, color = contentColor)
                Text(text = stateText, fontSize = 13.sp, fontWeight = FontWeight.Medium, color = subTextColor)
            }
        }
    }
}

@Composable
fun HomeKitActionButton(
    modifier: Modifier = Modifier,
    label: String,
    icon: ImageVector,
    isLoading: Boolean,
    onClick: () -> Unit
) {
    Card(
        onClick = { if (!isLoading) onClick() },
        modifier = modifier.height(56.dp),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = CardWhite)
    ) {
        Row(
            modifier = Modifier.fillMaxSize(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.Center
        ) {
            if (isLoading) {
                CircularProgressIndicator(modifier = Modifier.size(18.dp), color = TextDark, strokeWidth = 2.dp)
            } else {
                Icon(icon, contentDescription = null, tint = TextDark, modifier = Modifier.size(18.dp))
                Spacer(modifier = Modifier.width(8.dp))
                Text(text = label, fontSize = 14.sp, fontWeight = FontWeight.SemiBold, color = TextDark)
            }
        }
    }
}

@Composable
fun HomeKitClotheslineCard(
    isExtended: Boolean,
    isLoading: Boolean,
    onControl: (Boolean) -> Unit
) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(24.dp),
        colors = CardDefaults.cardColors(containerColor = CardWhite)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(20.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(
                    modifier = Modifier.size(48.dp).clip(CircleShape).background(DeviceOffGray),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(if (isExtended) Icons.Default.WbSunny else Icons.Default.WaterDrop, contentDescription = null, tint = TextGray)
                }
                Spacer(modifier = Modifier.width(16.dp))
                Column {
                    Text(text = "Giàn Phơi Cố Định", fontSize = 16.sp, fontWeight = FontWeight.SemiBold, color = TextDark)
                    Text(
                        text = if (isExtended) "Đang Mở" else "Đã Thu (Mưa)",
                        fontSize = 13.sp, color = TextGray
                    )
                }
            }

            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                FilledIconButton(
                    onClick = { onControl(false) },
                    modifier = Modifier.size(44.dp),
                    colors = IconButtonDefaults.filledIconButtonColors(
                        containerColor = if (!isExtended) PrimaryBlue else DeviceOffGray,
                        contentColor = if (!isExtended) Color.White else TextDark
                    )
                ) {
                    if (isLoading && !isExtended) CircularProgressIndicator(modifier = Modifier.size(20.dp), color = Color.White, strokeWidth = 2.dp)
                    else Icon(Icons.Default.ArrowBack, contentDescription = "Thu", modifier = Modifier.size(20.dp))
                }
                FilledIconButton(
                    onClick = { onControl(true) },
                    modifier = Modifier.size(44.dp),
                    colors = IconButtonDefaults.filledIconButtonColors(
                        containerColor = if (isExtended) PrimaryBlue else DeviceOffGray,
                        contentColor = if (isExtended) Color.White else TextDark
                    )
                ) {
                    if (isLoading && isExtended) CircularProgressIndicator(modifier = Modifier.size(20.dp), color = Color.White, strokeWidth = 2.dp)
                    else Icon(Icons.Default.ArrowForward, contentDescription = "Mở", modifier = Modifier.size(20.dp))
                }
            }
        }
    }
}

@Composable
fun ScheduleItemCard(schedule: Schedule, onDelete: () -> Unit) {
    val toolName = when(schedule.tool) {
        "fan_control" -> "Quạt máy"
        "living_room_lights_control" -> "Đèn Khách"
        "bedroom_lights_control" -> "Đèn Ngủ"
        "kitchen_lights_control" -> "Đèn Bếp"
        "clothesline_control" -> "Giàn phơi"
        else -> schedule.tool
    }
    
    val act = schedule.args["state"] ?: schedule.args["action"] ?: ""
    val actName = when(act) {
        "on" -> "Bật"
        "off" -> "Tắt"
        "extend" -> "Mở ra"
        "retract" -> "Thu vào"
        else -> act
    }

    Card(
        modifier = Modifier.fillMaxWidth().padding(bottom = 8.dp),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = CardWhite)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(16.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(text = schedule.time, fontSize = 22.sp, fontWeight = FontWeight.Bold, color = TextDark)
                Text(text = "$actName $toolName", fontSize = 14.sp, color = TextGray)
            }
            IconButton(onClick = onDelete) {
                Icon(Icons.Default.DeleteOutline, contentDescription = "Xoá", tint = AlertRed)
            }
        }
    }
}

@Composable
fun AddScheduleDialog(onDismiss: () -> Unit, onConfirm: (String, String, String) -> Unit) {
    var hour by remember { mutableStateOf("08") }
    var minute by remember { mutableStateOf("00") }
    var selectedTool by remember { mutableStateOf("fan_control") }
    var selectedState by remember { mutableStateOf("on") }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text("Thêm lịch mới") },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(16.dp)) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
                    OutlinedTextField(
                        value = hour, onValueChange = { hour = it },
                        label = { Text("Giờ (HH)") },
                        modifier = Modifier.weight(1f),
                        singleLine = true
                    )
                    Text(":", fontSize = 20.sp, fontWeight = FontWeight.Bold)
                    OutlinedTextField(
                        value = minute, onValueChange = { minute = it },
                        label = { Text("Phút (MM)") },
                        modifier = Modifier.weight(1f),
                        singleLine = true
                    )
                }

                // Dropdown for tool (simplified to radios for compose quick setup)
                Text("Thiết bị:", fontWeight = FontWeight.SemiBold)
                Column {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        RadioButton(selected = selectedTool == "fan_control", onClick = { selectedTool = "fan_control" })
                        Text("Quạt điện")
                    }
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        RadioButton(selected = selectedTool == "living_room_lights_control", onClick = { selectedTool = "living_room_lights_control" })
                        Text("Đèn khách")
                    }
                }

                Text("Hành động:", fontWeight = FontWeight.SemiBold)
                Row {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        RadioButton(selected = selectedState == "on", onClick = { selectedState = "on" })
                        Text("Bật")
                    }
                    Spacer(modifier = Modifier.width(16.dp))
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        RadioButton(selected = selectedState == "off", onClick = { selectedState = "off" })
                        Text("Tắt")
                    }
                }
            }
        },
        confirmButton = {
            TextButton(
                onClick = { 
                    val timeString = "${hour.padStart(2, '0')}:${minute.padStart(2, '0')}"
                    onConfirm(timeString, selectedTool, selectedState) 
                }
            ) {
                Text("Lưu")
            }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text("Hủy", color = TextGray) }
        }
    )
}

@Composable
fun ErrorCard(error: String) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = AlertRed.copy(alpha = 0.1f))
    ) {
        Row(modifier = Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Default.ErrorOutline, null, tint = AlertRed, modifier = Modifier.size(24.dp))
            Spacer(modifier = Modifier.width(12.dp))
            Text(error, color = AlertRed, fontSize = 13.sp, fontWeight = FontWeight.Medium)
        }
    }
}

@Composable
fun ConnectionBadge(isOnline: Boolean) {
    val color = if (isOnline) SuccessGreen else AlertRed
    Row(
        verticalAlignment = Alignment.CenterVertically,
        modifier = Modifier
            .clip(RoundedCornerShape(12.dp))
            .background(color.copy(alpha = 0.1f))
            .padding(horizontal = 10.dp, vertical = 6.dp)
    ) {
        Box(modifier = Modifier.size(6.dp).clip(CircleShape).background(color))
        Spacer(modifier = Modifier.width(6.dp))
        Text(
            text = if (isOnline) "Đã kết nối" else "Mất mạng",
            fontSize = 11.sp,
            fontWeight = FontWeight.Bold,
            color = color
        )
    }
}
