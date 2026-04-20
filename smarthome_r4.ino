// =====================================================================
// FREERTOS CONFIG OVERRIDES — phai dat TRUOC #include <Arduino_FreeRTOS.h>
// FreeRTOSConfig.h dung #ifndef nen cac define sau day se uu tien hon default.
// =====================================================================
#define configUSE_MUTEXES                                                      \
  1 // bat mutex API → priority inheritance (fix priority inversion)
#define configCHECK_FOR_STACK_OVERFLOW                                         \
  2 // method 2: kiem tra stack overflow runtime + watermark pattern
#define INCLUDE_uxTaskGetStackHighWaterMark                                    \
  1 // bat API uxTaskGetStackHighWaterMark()

#include <Arduino_FreeRTOS.h>
#include <EEPROM.h>
#include <WiFiClient.h>
#include <WiFiS3.h>
#include <cstdlib>
#include <cstring>

// =====================================================================
// PIN
// =====================================================================
#define FAN 2
#define LIGHT1 4
#define LIGHT2 5
#define LIGHT3 6
#define LIGHT_YARD 7
#define BUTTON_PIN 8
#define SERVO_PIN 10
#define BUZZER_PIN A2
#define RAIN_A0 A0
#define RAIN_D0 11
#define FLAME_D0 12
#define DHTPIN 13

// Hàm tự đọc DHT11 không chặn ngắt (tránh treo WiFi / FreeRTOS)
static bool readDHT11Custom(int pin, float &t, float &h) {
  uint8_t data[5] = {0, 0, 0, 0, 0};
  pinMode(pin, OUTPUT);
  digitalWrite(pin, LOW);
  vTaskDelay(pdMS_TO_TICKS(20)); // DHT11 cần kéo LOW >18ms
  digitalWrite(pin, HIGH);
  delayMicroseconds(30);
  pinMode(pin, INPUT_PULLUP);

  uint16_t loopCnt = 0;
  // Cho phep toi da moi vong lap ~1000us
  while (digitalRead(pin) == HIGH) { if (++loopCnt > 1000) return false; delayMicroseconds(1); }
  loopCnt = 0;
  while (digitalRead(pin) == LOW) { if (++loopCnt > 1000) return false; delayMicroseconds(1); }
  loopCnt = 0;
  while (digitalRead(pin) == HIGH) { if (++loopCnt > 1000) return false; delayMicroseconds(1); }

  for (int i = 0; i < 40; i++) {
    loopCnt = 0;
    while (digitalRead(pin) == LOW) { if (++loopCnt > 1000) return false; delayMicroseconds(1); }
    unsigned long timeCheck = micros();
    loopCnt = 0;
    while (digitalRead(pin) == HIGH) { if (++loopCnt > 1000) return false; delayMicroseconds(1); }
    if ((micros() - timeCheck) > 40) {
      data[i / 8] |= (1 << (7 - (i % 8)));
    }
  }

  if (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
    h = data[0] + data[1] / 10.0;
    t = data[2] + data[3] / 10.0;
    return true;
  }
  return false;
}

// Ngưỡng mưa có hysteresis — tránh nhiễu A0 quanh 1 giá trị làm servo kéo/ra
// liên tục
#define RAIN_PCT_RETRACT 55
#define RAIN_PCT_EXTEND 38

// Servo giàn phơi: 0°/90° tùy lắp cơ khí — trước đây bị ngược (khô thu / mưa
// mở)
#define CLOTHESLINE_EXTEND_DEG 90 // trời khô — mở giàn phơi
#define CLOTHESLINE_RETRACT_DEG 0 // trời mưa — thu vào

// =====================================================================
// WIFI / BRIDGE
// =====================================================================
const char *BRIDGE_HOST = "172.20.10.3";
const int BRIDGE_PORT = 8080;
#define EEPROM_FLAG 0
#define EEPROM_SSID 1
#define EEPROM_PASS 34
WiFiServer portalServer(80);

// Portal WiFi chay truoc khi start scheduler — chi dung delay(), khong String
// (tranh heap tren R4)
#define PORTAL_REQ_CAP 512u
#define PORTAL_BODY_CAP 384u
static char s_portalReq[PORTAL_REQ_CAP];
static char s_portalBody[PORTAL_BODY_CAP];

// =====================================================================
// SERVO THU CONG (khong dung Servo.h)
// =====================================================================
void servoWrite(int angle) {
  int us = map(angle, 0, 90, 1500, 500); // calib thuc te: 0°=1500us, 90°=500us
  digitalWrite(SERVO_PIN, HIGH);
  delayMicroseconds(us);
  digitalWrite(SERVO_PIN, LOW);
}

// Chu kỳ xung servo ~20 ms (50 Hz). KHÔNG dùng delay() — trên FreeRTOS nó =
// vTaskDelay → task lửa/HTTP chen vào → khoảng cách xung lệch → servo giật.
// Busy-wait μs giữ nhịp ổn định hơn.
#define SERVO_FRAME_US 20000

void servoSet(int angle, int durationMs) {
  unsigned long start = millis();
  while (millis() - start < (unsigned long)durationMs) {
    servoWrite(angle);
    vTaskDelay(pdMS_TO_TICKS(20)); // Thay busy-wait = yield RTOS, chống rớt mạng
  }
}

// =====================================================================
// SHARED STATE + ĐỒNG BỘ
// =====================================================================
SemaphoreHandle_t xMutex; // mutex (priority inheritance): bao ve shared state
SemaphoreHandle_t xServoSem; // mutex (priority inheritance): doc quyen servo
SemaphoreHandle_t
    xSerialSem; // mutex (priority inheritance): bao ve Serial output

// Task handles — dung de doc stack watermark trong taskHeartbeat
static TaskHandle_t hHB = NULL;
static TaskHandle_t hHTTP = NULL;
static TaskHandle_t hRain = NULL;
static TaskHandle_t hFire = NULL;

// serialTake dung timeout 5000ms thay vi portMAX_DELAY:
// neu mot task chet trong khi giu xSerialSem, cac task khac van hoat dong (chi
// mat 1 dong log) thay vi bi deadlock vinh vien.
static void serialTake() {
  if (xSerialSem)
    xSemaphoreTake(xSerialSem, pdMS_TO_TICKS(5000));
}

static void serialGive() {
  Serial.flush();
  if (xSerialSem)
    xSemaphoreGive(xSerialSem);
}

struct State {
  bool flame = false;
  bool fireAlarm = false;
  bool retracted = false; // true = đang thu (mưa); false = đang mở (khô)
  int rainPct = 0;
  int clotheslineAngle =
      CLOTHESLINE_EXTEND_DEG; // đồng bộ với task mưa + lệnh HTTP (tránh hold
                              // xung kéo ngược)
  // App bấm "thu" khi trời khô: không tự "mở" lại; app bấm "mở" thì trở lại
  // auto (khô → mở, mưa → thu)
  bool clotheslineManualRetractHold = false;
  float dhtTemp = 0.0;
  float dhtHum = 0.0;
  // Trạng thái thiết bị — dùng cho get_devices_status (digitalRead OUTPUT sai trên R4)
  bool fanOn       = false;
  bool light1On    = false; // phòng khách
  bool light2On    = false; // phòng ngủ
  bool light3On    = false; // nhà bếp
  bool lightYardOn = false; // sân
} state;

// Bo nho & stack (theo tai lieu / cau hinh core):
//   - RA4M1: 32 kB SRAM tong — https://docs.arduino.cc/hardware/uno-r4-wifi
//   - xTaskCreate uxStackDepth = so WORD (4 byte) —
//   https://www.freertos.org/a00125.html
//   - ArduinoCore-renesas (UNO WiFi R4): configTOTAL_HEAP_SIZE = 0x2000 (8 KiB)
//   cho heap FreeRTOS;
//     stack moi task + idle + timer lay tu heap do (FreeRTOSConfig.h).
//   - configUSE_MUTEXES=1 (override) → dung mutex API, co priority inheritance.
//   - stackHighWaterMark duoc log moi 30s trong taskHeartbeat — theo doi
//   runtime.
// HTTP/MCP: khong dung ArduinoJson — parse chuoi toi gian → 0 malloc JSON tren
// R4.
#define STACK_FIRE 192u
#define STACK_RAIN 384u
#define STACK_HTTP 768u
#define STACK_HB 128u

#define HTTP_POLL_BODY_CAP 768u
static char s_pollBody[HTTP_POLL_BODY_CAP];

// Ưu tiên: số càng cao càng được chạy trước (an toàn > mưa > mạng > LED)
#define PRIO_HEART 1
#define PRIO_HTTP 2
#define PRIO_RAIN 3
#define PRIO_FIRE 4

static void servoSetLocked(int angle, int durationMs) {
  if (!xServoSem) {
    servoSet(angle, durationMs);
    return;
  }
  if (xSemaphoreTake(xServoSem, portMAX_DELAY) != pdTRUE)
    return;
  servoSet(angle, durationMs);
  xSemaphoreGive(xServoSem);
}

// Khi đứng yên: chỉ 1 xung PWM (~1–2 ms), không lặp 20 ms mỗi 300 ms → bớt giật
// cơ khí rõ rệt.
static void servoHoldOnceLocked(int angle) {
  if (!xServoSem) {
    servoWrite(angle);
    return;
  }
  if (xSemaphoreTake(xServoSem, pdMS_TO_TICKS(100)) != pdTRUE)
    return;
  servoWrite(angle);
  xSemaphoreGive(xServoSem);
}

// =====================================================================
// FORWARD DECLARATIONS
// =====================================================================
bool loadWiFiConfig();
void saveWiFiConfig(const char *, const char *);
void clearWiFiConfig();
void maybeOfferWifiClearAtBoot();
void handlePortalClient(WiFiClient &);
void startWiFiOrPortal();
bool httpPoll();
bool httpPostResult(const char *id, const char *resultJson);
size_t executeCommand(const char *tool, const char *pollJson, char *out,
                      size_t outCap);
void taskFire(void *);
void taskRainServo(void *);
void taskHttpBridge(void *);
void taskHeartbeat(void *);

// =====================================================================
// RTOS — Task 1: Lửa + còi (chu kỳ ngắn, ưu tiên cao)
// =====================================================================
void taskFire(void * /*p*/) {
  pinMode(FLAME_D0, INPUT);
  bool buzzerOn = false;

  for (;;) {
    bool fire = (digitalRead(FLAME_D0) == LOW);

    if (fire && !buzzerOn) {
      buzzerOn = true;
      digitalWrite(BUZZER_PIN, HIGH);
      serialTake();
      Serial.println(F("[FIRE] CANH BAO LUA!"));
      serialGive();
    } else if (!fire && buzzerOn) {
      buzzerOn = false;
      digitalWrite(BUZZER_PIN, LOW);
      serialTake();
      Serial.println(F("[FIRE] An toan."));
      serialGive();
    }

    if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      state.flame = fire;
      state.fireAlarm = buzzerOn;
      xSemaphoreGive(xMutex);
    }

    vTaskDelay(pdMS_TO_TICKS(80));
  }
}

// =====================================================================
// RTOS — Task 2: Mưa + servo phơi đồ
// =====================================================================
void taskRainServo(void * /*p*/) {
  // Nhiều module mưa D0 kiểu open-drain: kéo lên nội bộ giảm nhiễu khi trời khô
  pinMode(RAIN_D0, INPUT_PULLUP);
  pinMode(SERVO_PIN, OUTPUT);
  digitalWrite(SERVO_PIN, LOW);

  bool retracted = false;
  int curAngle = CLOTHESLINE_EXTEND_DEG;
  unsigned long lastDbg = 0;
  unsigned long lastServoRefreshMs = 0;
  const unsigned long kServoIdleRefreshMs = 1500;
  int8_t d0Debounce = 0; // dem so lan D0=1 lien tiep de chong nhieu

  servoSetLocked(CLOTHESLINE_EXTEND_DEG, 600);
  lastServoRefreshMs = millis();

  for (;;) {
    int rA = analogRead(RAIN_A0);
    bool rDig = (digitalRead(RAIN_D0) == LOW);
    int rPct = constrain(map(rA, 1023, 0, 0, 100), 0, 100);

    // Debounce D0: can 4 lan lien tiep (4*300ms=~1.2s) moi xac nhan la mua
    // Tranh nhieu cam bien (D0 dao dong 0/1) lam servo keo/mo lien tuc khi troi
    // kho
    if (rDig) {
      if (d0Debounce < 4)
        d0Debounce++;
    } else {
      if (d0Debounce > 0)
        d0Debounce--;
    }
    bool rDigStable = (d0Debounce >= 4);

    // Doc state sau cam bien, truoc logic chuyen trang thai — giam cua so bi
    // HTTP chen. Neu take mutex that bai: KHONG duoc de manualRetractHold mac
    // dinh false + retracted stale (se vo nhieu "Mo ra" ngay sau lenh retract).
    bool manualRetractHold = false;
    if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(200)) != pdTRUE) {
      vTaskDelay(pdMS_TO_TICKS(300));
      continue;
    }
    retracted = state.retracted;
    curAngle = state.clotheslineAngle;
    manualRetractHold = state.clotheslineManualRetractHold;
    xSemaphoreGive(xMutex);

    bool shouldRetract;
    if (!retracted) {
      shouldRetract = (rPct >= RAIN_PCT_RETRACT || rDigStable);
    } else {
      shouldRetract = (rPct >= RAIN_PCT_EXTEND || rDigStable);
    }

    bool moved = false;
    if (shouldRetract && !retracted) {
      retracted = true;
      curAngle = CLOTHESLINE_RETRACT_DEG;
      serialTake();
      Serial.print(F("[RAIN] Thu vao pct="));
      Serial.print(rPct);
      Serial.print(F(" A0="));
      Serial.println(rA);
      serialGive();
      servoSetLocked(CLOTHESLINE_RETRACT_DEG, 800);
      moved = true;
    } else if (!shouldRetract && retracted && !manualRetractHold) {
      retracted = false;
      curAngle = CLOTHESLINE_EXTEND_DEG;
      serialTake();
      Serial.print(F("[RAIN] Mo ra pct="));
      Serial.print(rPct);
      Serial.print(F(" A0="));
      Serial.println(rA);
      serialGive();
      servoSetLocked(CLOTHESLINE_EXTEND_DEG, 800);
      moved = true;
    }

    if (moved) {
      lastServoRefreshMs = millis();
    } else if (millis() - lastServoRefreshMs >= kServoIdleRefreshMs) {
      lastServoRefreshMs = millis();
      servoHoldOnceLocked(curAngle);
    }

    // Chi ghi vi tri khi task mua thuc su dich servo; neu khong thi lay lai tu
    // state de lenh HTTP giua vong khong bi ghi de (tranh thu/mo lon xon).
    if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      state.rainPct = rPct;
      if (moved) {
        state.retracted = retracted;
        state.clotheslineAngle = curAngle;
      } else {
        retracted = state.retracted;
        curAngle = state.clotheslineAngle;
      }
      xSemaphoreGive(xMutex);
    }

    unsigned long now = millis();
    if (now - lastDbg >= 2000) {
      lastDbg = now;
      serialTake();
      Serial.print(F("[RAIN] dbg A0="));
      Serial.print(rA);
      Serial.print(F(" pct="));
      Serial.print(rPct);
      Serial.print(F(" D0="));
      Serial.print(rDig ? '1' : '0');
      Serial.print(F(" dbc="));
      Serial.print(d0Debounce);
      Serial.print(F(" wantRet="));
      Serial.print(shouldRetract ? '1' : '0');
      Serial.print(F(" ret="));
      Serial.print(retracted ? '1' : '0');
      Serial.print(F(" ang="));
      Serial.print(curAngle);
      Serial.print(F(" manRet="));
      Serial.println(manualRetractHold ? '1' : '0');
      serialGive();
    }

    vTaskDelay(pdMS_TO_TICKS(300));
  }
}

// =====================================================================
// RTOS — Task 3: HTTP poll MCP bridge (đèn, quạt, giàn phơi) — parse JSON tối
// giản, không thư viện JSON
// =====================================================================
static bool jsonPollIsNone(const char *j) {
  return strstr(j, "\"action\":\"none\"") != nullptr ||
         strstr(j, "\"action\": \"none\"") != nullptr;
}

static bool jsonExtractTool(const char *j, char *out, size_t cap) {
  const char *p = strstr(j, "\"tool\":\"");
  if (!p)
    return false;
  p += 8;
  size_t i = 0;
  while (*p && *p != '"' && i + 1 < cap)
    out[i++] = *p++;
  out[i] = '\0';
  return i > 0;
}

static bool jsonExtractId(const char *j, char *out, size_t cap) {
  const char *p = strstr(j, "\"id\":\"");
  if (p) {
    p += 6;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < cap)
      out[i++] = *p++;
    out[i] = '\0';
    return i > 0;
  }
  p = strstr(j, "\"id\":");
  if (!p)
    return false;
  p += 5;
  while (*p == ' ' || *p == '\t')
    p++;
  if (*p == '"') {
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i + 1 < cap)
      out[i++] = *p++;
    out[i] = '\0';
    return i > 0;
  }
  char *end = nullptr;
  unsigned long v = strtoul(p, &end, 10);
  (void)end;
  snprintf(out, cap, "%lu", v);
  return out[0] != '\0';
}

void taskHttpBridge(void * /*p*/) {
  char idBuf[40];
  char toolBuf[48];

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (!httpPoll()) {
      serialTake();
      Serial.println(F("[HTTP] poll fail (ko ket duoc bridge)"));
      serialGive();
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    if (jsonPollIsNone(s_pollBody)) {
      // Poll OK nhung khong co lenh — binh thuong, khong can log
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }

    idBuf[0] = '\0';
    toolBuf[0] = '\0';
    if (!jsonExtractTool(s_pollBody, toolBuf, sizeof(toolBuf))) {
      vTaskDelay(pdMS_TO_TICKS(200));
      continue;
    }
    jsonExtractId(s_pollBody, idBuf, sizeof(idBuf));

    serialTake();
    Serial.print(F("[HTTP] Lenh: "));
    Serial.println(toolBuf);
    serialGive();

    char resultBuf[320];
    resultBuf[0] = '\0';
    size_t rl =
        executeCommand(toolBuf, s_pollBody, resultBuf, sizeof(resultBuf));
    if (rl > 0 && idBuf[0] != '\0')
      httpPostResult(idBuf, resultBuf);

    serialTake();
    Serial.print(F("[HTTP] Done: "));
    Serial.println(resultBuf);
    serialGive();

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

// =====================================================================
// RTOS — Task 4: Nhịp tim hệ thống (LED built-in)
// =====================================================================
void taskHeartbeat(void * /*p*/) {
  pinMode(LED_BUILTIN, OUTPUT);
  uint8_t tick = 0;
  uint16_t wmarkTick = 0;
  for (;;) {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));

    static uint8_t dhtTick = 0;
    if (++dhtTick >= 2) {
      dhtTick = 0;
      float t = 0, h = 0;
      if (readDHT11Custom(DHTPIN, t, h)) {
        if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
          state.dhtTemp = t;
          state.dhtHum = h;
          xSemaphoreGive(xMutex);
        }
      }
    }

    // [HB] alive — moi 10 giay
    if (++tick >= 10) {
      tick = 0;
      serialTake();
      Serial.println(F("[HB] alive"));
      serialGive();
    }

    // Stack High Watermark — moi 30 giay
    // HWM = so WORD (4 byte) con lai chua dung den (so cang nho = stack cang
    // day) Neu HWM < 20 → nguy hiem; < 10 → co the tran bat cu luc nao
    if (++wmarkTick >= 30) {
      wmarkTick = 0;
      serialTake();
      Serial.print(F("[HB] StackHWM(words) HB="));
      Serial.print(
          uxTaskGetStackHighWaterMark(NULL)); // NULL = task hien tai (HB)
      Serial.print(F(" HTTP="));
      Serial.print(uxTaskGetStackHighWaterMark(hHTTP));
      Serial.print(F(" Rain="));
      Serial.print(uxTaskGetStackHighWaterMark(hRain));
      Serial.print(F(" Fire="));
      Serial.println(uxTaskGetStackHighWaterMark(hFire));
      serialGive();
    }

    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// =====================================================================
// HTTP — bỏ qua header tới body (không dùng String)
// =====================================================================
// Chỉ coi là hết header sau khi đã đọc ít nhất 1 dòng (tránh byte '\n' lạc đầu
// stream).
static bool skipToHttpBody(WiFiClient &client) {
  char line[128];
  size_t li = 0;
  bool gotLine = false;
  unsigned long t0 = millis();
  while (millis() - t0 < 4000) {
    if (!client.available()) {
      vTaskDelay(pdMS_TO_TICKS(2));
      if (!client.connected() && !client.available())
        return false;
      continue;
    }
    char c = (char)client.read();
    if (c == '\n') {
      if (li < sizeof(line))
        line[li] = '\0';
      else
        line[sizeof(line) - 1] = '\0';
      if (gotLine && (li == 0 || strcmp(line, "\r") == 0)) {
        return true;
      }
      gotLine = true;
      li = 0;
      continue;
    }
    if (li < sizeof(line) - 1)
      line[li++] = c;
  }
  return false;
}

// bridge.py /poll long-poll tối đa ~8s → chờ byte đầu > 8s
bool httpPoll() {
  s_pollBody[0] = '\0';

  WiFiClient client;
  if (!client.connect(BRIDGE_HOST, BRIDGE_PORT))
    return false;

  const unsigned long kPollFirstByteMs = 12000;

  client.print(F("GET /poll HTTP/1.1\r\nHost: "));
  client.print(BRIDGE_HOST);
  client.print(':');
  client.print(BRIDGE_PORT);
  client.print(F("\r\nConnection: close\r\n\r\n"));
  client.flush();

  unsigned long t = millis();
  while (!client.available() && millis() - t < kPollFirstByteMs)
    vTaskDelay(pdMS_TO_TICKS(10));
  if (!client.available()) {
    client.stop();
    return false;
  }

  if (!skipToHttpBody(client)) {
    client.stop();
    return false;
  }

  size_t n = 0;
  unsigned long t2 = millis();
  while (millis() - t2 < 2000) {
    while (client.available() && n + 1 < HTTP_POLL_BODY_CAP) {
      s_pollBody[n++] = (char)client.read();
      t2 = millis();
    }
    if (n + 1 >= HTTP_POLL_BODY_CAP)
      break;
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  s_pollBody[n] = '\0';
  client.stop();

  while (n > 0 && (s_pollBody[n - 1] == ' ' || s_pollBody[n - 1] == '\r' ||
                   s_pollBody[n - 1] == '\n'))
    s_pollBody[--n] = '\0';

  return n > 0;
}

bool httpPostResult(const char *id, const char *resultJson) {
  if (!id || !resultJson)
    return false;

  char body[384];
  int bl = snprintf(body, sizeof(body), "{\"id\":\"%.32s\",\"result\":%s}", id,
                    resultJson);
  if (bl <= 0 || (size_t)bl >= sizeof(body))
    return false;

  WiFiClient client;
  if (!client.connect(BRIDGE_HOST, BRIDGE_PORT))
    return false;

  client.print(F("POST /result HTTP/1.1\r\nHost: "));
  client.print(BRIDGE_HOST);
  client.print(':');
  client.print(BRIDGE_PORT);
  client.print(F("\r\nContent-Type: application/json\r\nContent-Length: "));
  client.print((int)strlen(body));
  client.print(F("\r\nConnection: close\r\n\r\n"));
  client.print(body);
  client.flush();

  unsigned long t = millis();
  while (!client.available() && millis() - t < 2000)
    vTaskDelay(pdMS_TO_TICKS(10));
  client.stop();
  return true;
}

// =====================================================================
// EXECUTE — đèn từng phòng + quạt + giàn phơi (parse từ nguyên bản JSON poll)
// =====================================================================
// 1 = on, 0 = off, -1 = không thấy
static int parseStateOnOff(const char *j) {
  const char *p = strstr(j, "\"state\":\"");
  if (p) {
    p += 9;
    if (!strncmp(p, "on\"", 3))
      return 1;
    if (!strncmp(p, "off\"", 4))
      return 0;
    return -1;
  }
  p = strstr(j, "\"state\": \"");
  if (p) {
    p += 10;
    if (!strncmp(p, "on\"", 3))
      return 1;
    if (!strncmp(p, "off\"", 4))
      return 0;
  }
  return -1;
}

// Trả về 1=retract, 2=extend, 0=không khớp (chỉ tìm trong args, trước
// "action":"execute" thường không lẫn)
static int parseClotheslineAction(const char *j) {
  if (strstr(j, "\"action\":\"retract\"") ||
      strstr(j, "\"action\": \"retract\""))
    return 1;
  if (strstr(j, "\"action\":\"extend\"") || strstr(j, "\"action\": \"extend\""))
    return 2;
  return 0;
}

size_t executeCommand(const char *tool, const char *pollJson, char *out,
                      size_t outCap) {
  if (!out || outCap < 32 || !tool)
    return 0;

  if (!strcmp(tool, "fan_control")) {
    int st = parseStateOnOff(pollJson);
    if (st < 0) {
      snprintf(out, outCap, "{\"success\":false,\"error\":\"missing_state\"}");
      return strlen(out);
    }
    digitalWrite(FAN, st == 1 ? HIGH : LOW);
    if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      state.fanOn = (st == 1);
      xSemaphoreGive(xMutex);
    }
    serialTake();
    Serial.print(F("[FAN] "));
    Serial.println(st == 1 ? "on" : "off");
    serialGive();
    snprintf(out, outCap,
             "{\"device\":\"fan\",\"state\":\"%s\",\"success\":true}",
             st == 1 ? "on" : "off");
    return strlen(out);
  }
  if (!strcmp(tool, "living_room_lights_control")) {
    int st = parseStateOnOff(pollJson);
    if (st < 0) {
      snprintf(out, outCap, "{\"success\":false,\"error\":\"missing_state\"}");
      return strlen(out);
    }
    digitalWrite(LIGHT1, st == 1 ? HIGH : LOW);
    if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      state.light1On = (st == 1);
      xSemaphoreGive(xMutex);
    }
    snprintf(
        out, outCap,
        "{\"device\":\"living_room_light\",\"state\":\"%s\",\"success\":true}",
        st == 1 ? "on" : "off");
    return strlen(out);
  }
  if (!strcmp(tool, "bedroom_lights_control")) {
    int st = parseStateOnOff(pollJson);
    if (st < 0) {
      snprintf(out, outCap, "{\"success\":false,\"error\":\"missing_state\"}");
      return strlen(out);
    }
    digitalWrite(LIGHT2, st == 1 ? HIGH : LOW);
    if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      state.light2On = (st == 1);
      xSemaphoreGive(xMutex);
    }
    snprintf(out, outCap,
             "{\"device\":\"bedroom_light\",\"state\":\"%s\",\"success\":true}",
             st == 1 ? "on" : "off");
    return strlen(out);
  }
  if (!strcmp(tool, "kitchen_lights_control")) {
    int st = parseStateOnOff(pollJson);
    if (st < 0) {
      snprintf(out, outCap, "{\"success\":false,\"error\":\"missing_state\"}");
      return strlen(out);
    }
    digitalWrite(LIGHT3, st == 1 ? HIGH : LOW);
    if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      state.light3On = (st == 1);
      xSemaphoreGive(xMutex);
    }
    snprintf(out, outCap,
             "{\"device\":\"kitchen_light\",\"state\":\"%s\",\"success\":true}",
             st == 1 ? "on" : "off");
    return strlen(out);
  }
  if (!strcmp(tool, "get_environment")) {
    float t = 0, h = 0;
    if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      t = state.dhtTemp;
      h = state.dhtHum;
      xSemaphoreGive(xMutex);
    }
    snprintf(out, outCap,
             "{\"device\":\"dht11\",\"temperature\":%.1f,\"humidity\":%.1f,\"success\":true}",
             t, h);
    return strlen(out);
  }
  if (!strcmp(tool, "get_sensors_status")) {
    bool fire = false;
    bool fireAr = false;
    bool rainRetract = false;
    int rainP = 0;
    if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      fire = state.flame;
      fireAr = state.fireAlarm;
      rainRetract = state.retracted;
      rainP = state.rainPct;
      xSemaphoreGive(xMutex);
    }
    // Xác định mức mưa bằng text
    const char* rainStatus;
    if (rainP >= 70)       rainStatus = "mua to";
    else if (rainP >= 40)  rainStatus = "mua nhe";
    else if (rainP >= 15)  rainStatus = "am uot nhe";
    else                   rainStatus = "troi kho rao";

    const char* fireStatus  = fire   ? "phat hien lua chay - nguy hiem" : "an toan khong co lua";
    const char* alarmStatus = fireAr ? "coi bao dong dang ket" : "coi im lang";
    const char* clothStatus = rainRetract ? "gian phoi da thu vao (do mua)" : "gian phoi dang mo ra (troi kho)";

    snprintf(out, outCap,
             "{\"device\":\"sensors\","
             "\"rain_percent\":%d,\"rain_status\":\"%s\","
             "\"fire\":%s,\"fire_status\":\"%s\","
             "\"fire_alarm\":%s,\"alarm_status\":\"%s\","
             "\"clothesline_retracted\":%s,\"clothesline_status\":\"%s\","
             "\"success\":true}",
             rainP, rainStatus,
             fire   ? "true" : "false", fireStatus,
             fireAr ? "true" : "false", alarmStatus,
             rainRetract ? "true" : "false", clothStatus);
    return strlen(out);
  }
  if (!strcmp(tool, "clothesline_control")) {
    int ac = parseClotheslineAction(pollJson);
    if (ac == 1) {
      if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        state.retracted = true;
        state.clotheslineAngle = CLOTHESLINE_RETRACT_DEG;
        state.clotheslineManualRetractHold = true;
        xSemaphoreGive(xMutex);
      }
      servoSetLocked(CLOTHESLINE_RETRACT_DEG, 800);
      snprintf(out, outCap,
               "{\"device\":\"clothesline\",\"action\":\"retract\",\"success\":"
               "true}");
    } else if (ac == 2) {
      if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        state.retracted = false;
        state.clotheslineAngle = CLOTHESLINE_EXTEND_DEG;
        state.clotheslineManualRetractHold = false;
        xSemaphoreGive(xMutex);
      }
      servoSetLocked(CLOTHESLINE_EXTEND_DEG, 800);
      snprintf(out, outCap,
               "{\"device\":\"clothesline\",\"action\":\"extend\",\"success\":"
               "true}");
    } else {
      snprintf(out, outCap, "{\"success\":false,\"error\":\"missing_action\"}");
    }
    return strlen(out);
  }

  if (!strcmp(tool, "get_devices_status")) {
    bool fan = false, lt1 = false, lt2 = false, lt3 = false, ltY = false;
    if (xSemaphoreTake(xMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      fan = state.fanOn;
      lt1 = state.light1On;
      lt2 = state.light2On;
      lt3 = state.light3On;
      ltY = state.lightYardOn;
      xSemaphoreGive(xMutex);
    }
    snprintf(out, outCap,
             "{\"fan\":\"%s\","
             "\"living_room_light\":\"%s\","
             "\"bedroom_light\":\"%s\","
             "\"kitchen_light\":\"%s\","
             "\"yard_light\":\"%s\","
             "\"summary\":\"%s - phong khach %s - phong ngu %s - bep %s - san %s\","
             "\"success\":true}",
             fan ? "on" : "off",
             lt1 ? "on" : "off",
             lt2 ? "on" : "off",
             lt3 ? "on" : "off",
             ltY ? "on" : "off",
             fan ? "quat dang chay" : "quat tat",
             lt1 ? "sang" : "tat",
             lt2 ? "sang" : "tat",
             lt3 ? "sang" : "tat",
             ltY ? "sang" : "tat");
    return strlen(out);
  }

  snprintf(out, outCap, "{\"success\":false,\"error\":\"Unknown: %.48s\"}",
           tool);
  return strlen(out);
}

// =====================================================================
// WIFI PORTAL
// =====================================================================
bool loadWiFiConfig() {
  EEPROM.begin();
  if (EEPROM.read(EEPROM_FLAG) != 0xAB)
    return false;
  char ssid[33] = {0}, pass[65] = {0};
  for (int i = 0; i < 32; i++)
    ssid[i] = EEPROM.read(EEPROM_SSID + i);
  for (int i = 0; i < 64; i++)
    pass[i] = EEPROM.read(EEPROM_PASS + i);
  if (strlen(ssid) == 0)
    return false;

  Serial.print("[WiFi] Ket noi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, pass);

  // Chờ connected trước
  int r = 0;
  while (WiFi.status() != WL_CONNECTED && r < 40) {
    delay(500);
    Serial.print(".");
    r++;
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("[WiFi] Khong ket noi duoc!"));
    WiFi.disconnect();
    delay(100);
    return false;
  }

  // Chờ thêm cho DHCP cấp IP thực sự (khác 0.0.0.0)
  r = 0;
  while (WiFi.localIP() == IPAddress(0, 0, 0, 0) && r < 20) {
    delay(500);
    Serial.print("~");
    r++;
  }
  Serial.println();

  if (WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
    Serial.println(F("[WiFi] Khong lay duoc IP!"));
    WiFi.disconnect();
    delay(100);
    return false;
  }

  Serial.print("[WiFi] IP: ");
  Serial.println(WiFi.localIP());
  return true;
}

void saveWiFiConfig(const char *ssid, const char *pass) {
  EEPROM.begin();
  EEPROM.write(EEPROM_FLAG, 0xAB);
  for (int i = 0; i < 32; i++)
    EEPROM.write(EEPROM_SSID + i, i < (int)strlen(ssid) ? ssid[i] : 0);
  for (int i = 0; i < 64; i++)
    EEPROM.write(EEPROM_PASS + i, i < (int)strlen(pass) ? pass[i] : 0);
}

// Xoa het vung SSID/pass (EEPROM ao tren R4 — chi xoa co 0x00 khong du sach)
void clearWiFiConfig() {
  EEPROM.begin();
  EEPROM.write(EEPROM_FLAG, 0x00);
  for (int i = 0; i < 32; i++)
    EEPROM.write(EEPROM_SSID + i, 0);
  for (int i = 0; i < 64; i++)
    EEPROM.write(EEPROM_PASS + i, 0);
}

// Reset nut tren board KHONG xoa EEPROM — SSID/pass van con, nen boot lai khong
// hoi WiFi. Cach nhap lai: giu nut 3s trong loop() (may reset); hoac vao portal
// khi loadWiFiConfig that bai.
void maybeOfferWifiClearAtBoot() {
  unsigned long t = millis();
  while (digitalRead(BUTTON_PIN) == LOW && millis() - t < 5000)
    delay(20);
  Serial.println(
      F("[WiFi] Luu y: nut RESET tren board khong xoa WiFi (EEPROM)."));
  Serial.println(F("[WiFi] Muon nhap lai SSID: giu nut ~3 giay khi dang chay "
                   "de xoa va khoi dong lai."));
  Serial.flush();
}

static bool portalReadHeaders(WiFiClient &client, char *buf, size_t cap) {
  if (cap < 8)
    return false;
  size_t n = 0;
  unsigned long t0 = millis();
  while (client.connected() && millis() - t0 < 2000) {
    if (client.available()) {
      char c = (char)client.read();
      if (n + 1 >= cap) {
        buf[cap - 1] = '\0';
        return strstr(buf, "\r\n\r\n") != nullptr;
      }
      buf[n++] = c;
      buf[n] = '\0';
      if (strstr(buf, "\r\n\r\n") != nullptr)
        return true;
    } else {
      delay(1);
    }
  }
  buf[n] = '\0';
  return strstr(buf, "\r\n\r\n") != nullptr;
}

static int portalContentLength(const char *headers) {
  const char *p = strstr(headers, "Content-Length:");
  if (!p)
    p = strstr(headers, "content-length:");
  if (!p)
    return 0;
  p = strchr(p, ':');
  if (!p)
    return 0;
  ++p;
  while (*p == ' ' || *p == '\t')
    ++p;
  return (int)strtol(p, nullptr, 10);
}

static void portalReadBody(WiFiClient &client, char *buf, size_t cap,
                           int contentLen) {
  buf[0] = '\0';
  if (contentLen <= 0)
    return;
  int want = contentLen;
  if (want >= (int)cap)
    want = (int)cap - 1;
  int n = 0;
  unsigned long t0 = millis();
  while (n < want && millis() - t0 < 2000) {
    if (client.available())
      buf[n++] = (char)client.read();
    else
      delay(1);
  }
  buf[n] = '\0';
}

static void portalUrlDecode(const char *in, size_t inLen, char *out,
                            size_t outCap) {
  size_t o = 0;
  for (size_t i = 0; i < inLen && o + 1 < outCap; ++i) {
    if (in[i] == '+')
      out[o++] = ' ';
    else if (in[i] == '%' && i + 2 < inLen) {
      char h[3] = {in[i + 1], in[i + 2], '\0'};
      out[o++] = (char)strtol(h, nullptr, 16);
      i += 2;
    } else
      out[o++] = in[i];
  }
  out[o] = '\0';
}

static void portalParseSaveForm(const char *body, char *ssid, size_t ssidCap,
                                char *pass, size_t passCap) {
  ssid[0] = '\0';
  pass[0] = '\0';
  const char *s = strstr(body, "ssid=");
  if (s) {
    s += 5;
    const char *amp = strchr(s, '&');
    size_t len = amp ? (size_t)(amp - s) : strlen(s);
    portalUrlDecode(s, len, ssid, ssidCap);
  }
  const char *pw = strstr(body, "pass=");
  if (pw) {
    pw += 5;
    size_t len = strlen(pw);
    portalUrlDecode(pw, len, pass, passCap);
  }
}

void handlePortalClient(WiFiClient &client) {
  if (!portalReadHeaders(client, s_portalReq, PORTAL_REQ_CAP)) {
    client.stop();
    return;
  }

  s_portalBody[0] = '\0';
  if (strncmp(s_portalReq, "POST", 4) == 0) {
    int cl = portalContentLength(s_portalReq);
    portalReadBody(client, s_portalBody, PORTAL_BODY_CAP, cl);
  }

  if (strstr(s_portalReq, "POST /save") != nullptr) {
    char ssid[33];
    char pass[65];
    portalParseSaveForm(s_portalBody, ssid, sizeof(ssid), pass, sizeof(pass));
    if (ssid[0] != '\0') {
      saveWiFiConfig(ssid, pass);
      client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<h2>Da "
                   "luu! Khoi dong lai...</h2>");
      client.flush();
      delay(500);
      NVIC_SystemReset();
    } else {
      client.print("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\n\r\n<h2>Loi: "
                   "SSID trong!</h2><a href='/'>Quay lai</a>");
    }
  } else {
    client.print(
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n"
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' "
        "content='width=device-width,initial-scale=1'></head><body>"
        "<h2>SmartHome R4</h2><form method='POST' action='/save'>"
        "<p>SSID:<br><input name='ssid'></p>"
        "<p>Password:<br><input name='pass' type='password'></p>"
        "<button type='submit'>Luu</button></form></body></html>");
  }
  client.stop();
}

void startWiFiOrPortal() {
  if (loadWiFiConfig())
    return;
  Serial.println(F("[WiFi] Mo portal AP: SmartHome-R4 / pass 12345678"));
  Serial.flush();
  WiFi.disconnect();
  delay(200);
  WiFi.beginAP("SmartHome-R4", "12345678");
  delay(1000);
  portalServer.begin();
  // Khong thoat bang nut nhe: neu break se chay tiep ma khong co STA — chi luu
  // qua web (reset).
  for (;;) {
    WiFiClient c = portalServer.available();
    if (c)
      handlePortalClient(c);
    delay(10);
  }
}

// =====================================================================
// SETUP
// =====================================================================
void setup() {
  // 115200 + không chờ USB: mở Serial Monitor sau khi boot vẫn xem được log;
  // flush giúp CDC trên R4
  Serial.begin(115200);
  delay(400);
  Serial.println(F("\n=== SmartHome R4 boot ==="));
  Serial.flush();
  
  analogReadResolution(10);
  int outs[] = {FAN, LIGHT1, LIGHT2, LIGHT3, LIGHT_YARD, BUZZER_PIN};
  for (int p : outs) {
    pinMode(p, OUTPUT);
    digitalWrite(p, LOW);
  }
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  delay(50);

  maybeOfferWifiClearAtBoot();

  startWiFiOrPortal();

  // Mutex co priority inheritance (configUSE_MUTEXES=1 da duoc set o dau file)
  // xSemaphoreCreateMutex() tra ve mutex da san sang (gia tri ban dau =
  // available) KHONG can goi xSemaphoreGive() sau khi create — khac voi binary
  // semaphore!
  xMutex = xSemaphoreCreateMutex();
  xServoSem = xSemaphoreCreateMutex();
  xSerialSem = xSemaphoreCreateMutex();

  if (!xMutex || !xServoSem || !xSerialSem) {
    Serial.println(F("[SYS] FATAL: Khong tao duoc mutex — heap khong du?"));
    Serial.flush();
    for (;;)
      ; // halt
  }

  Serial.println(
      F("[SYS] WiFi/MCP tasks sap chay — dat Serial Monitor 115200 baud"));
  Serial.flush();

  BaseType_t ok = pdTRUE;
  ok &= (xTaskCreate(taskHeartbeat, "HB", STACK_HB, NULL, PRIO_HEART, &hHB) ==
         pdPASS);
  ok &= (xTaskCreate(taskHttpBridge, "HTTP", STACK_HTTP, NULL, PRIO_HTTP,
                     &hHTTP) == pdPASS);
  ok &= (xTaskCreate(taskRainServo, "Rain", STACK_RAIN, NULL, PRIO_RAIN,
                     &hRain) == pdPASS);
  ok &= (xTaskCreate(taskFire, "Fire", STACK_FIRE, NULL, PRIO_FIRE, &hFire) ==
         pdPASS);

  Serial.print("[RTOS] Tasks (HB,HTTP,Rain,Fire): ");
  Serial.println(ok ? "OK" : "FAIL");

  Serial.println("[SYS] Bat dau...");
  vTaskStartScheduler(); // BẮT BUỘC trên R4
}

// =====================================================================
// LOOP: chỉ xử lý nút (HTTP đã chuyển sang taskHttpBridge)
// =====================================================================
void loop() {
  if (digitalRead(BUTTON_PIN) == LOW) {
    delay(3000);
    if (digitalRead(BUTTON_PIN) == LOW) {
      clearWiFiConfig();
      delay(500);
      NVIC_SystemReset();
    }
  }
  vTaskDelay(pdMS_TO_TICKS(50));
}

// =====================================================================
// STACK OVERFLOW HOOK — goi boi FreeRTOS khi phat hien stack tran
// (configCHECK_FOR_STACK_OVERFLOW=2 da duoc enable o dau file)
// CANH BAO: Hook nay chay trong interrupt context — khong duoc goi
// bat ky ham nao co the block (Serial.print co the khong an toan);
// ta chi LED + halt, tranh undefined behavior lan truyen.
// =====================================================================
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask,
                                              char *pcTaskName) {
  (void)xTask;
  // Tat tat ca interrupt, blink LED SOS 3-3-3 de bao hieu
  taskDISABLE_INTERRUPTS();
  pinMode(LED_BUILTIN, OUTPUT);
  // Halt — khong the tiep tuc an toan khi stack bi tran
  for (;;) {
    // SOS: 3 nhan + 3 dai + 3 nhan
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(200);
      digitalWrite(LED_BUILTIN, LOW);
      delay(200);
    }
    delay(400);
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(600);
      digitalWrite(LED_BUILTIN, LOW);
      delay(200);
    }
    delay(400);
    for (int i = 0; i < 3; i++) {
      digitalWrite(LED_BUILTIN, HIGH);
      delay(200);
      digitalWrite(LED_BUILTIN, LOW);
      delay(200);
    }
    delay(1000);
  }
}
