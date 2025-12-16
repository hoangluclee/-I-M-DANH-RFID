#include <WiFi.h>
#include <WebServer.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Preferences.h>
#include <ESP_Mail_Client.h>
#include <nvs_flash.h>
#include <nvs.h>
#include "time.h"
#include <vector>
#include <ESP32Servo.h>

// ====== 1. CẤU HÌNH WIFI ======
const char* WIFI_SSID = "Luc";      
const char* WIFI_PASS = "12345678";        

// ====== 2. CẤU HÌNH EMAIL ======
#define EMAIL_GUI      "lehoangluc246@gmail.com"  
#define EMAIL_MATKHAU  "qroldjdgxauhiexf"         
#define EMAIL_NHAN     "lehoangluc.cv@gmail.com"  

// ====== 3. CHÂN KẾT NỐI ======
#define SS_PIN    5
#define RST_PIN   27 
#define SPI_SCK   18
#define SPI_MISO  19
#define SPI_MOSI  23
#define SERVO_PIN 4  // Chân điều khiển Servo

// ====== 4. CẤU HÌNH CỬA VÀ THỜI GIAN ======
#define DOOR_OPEN_ANGLE  90   
#define DOOR_CLOSE_ANGLE 0    
#define DOOR_DELAY_MS    5000 // Thời gian mở cửa (5 giây)
const char* ntpServer = "pool.ntp.org";
const long  gmtOffset_sec = 25200; // GMT+7
const int   daylightOffset_sec = 0; // <<< ĐÃ FIX LỖI KHÔNG KHAI BÁO >>>

struct HistoryLog {
  String time;
  String uid;
  String name;
  String status;
};

std::vector<HistoryLog> accessLogs; 

MFRC522 rfid(SS_PIN, RST_PIN);
WebServer server(80);
Preferences preferences;
SMTPSession smtp;
ESP_Mail_Session sessionConfig;
SMTP_Message emailMsg;
Servo doorServo; 

String lastUid = "";
String lastName = "";
unsigned long lastTimeMillis = 0;

bool isDoorOpen = false;
unsigned long doorTimer = 0;

// --- HÀM LẤY GIỜ ---
String getLocalTimeStr() {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return "--:--";
  char timeStringBuff[50];
  strftime(timeStringBuff, sizeof(timeStringBuff), "%H:%M:%S %d/%m", &timeinfo);
  return String(timeStringBuff);
}

String uidToHex(byte *buffer, byte bufferSize) {
  String uidStr = "";
  for (byte i = 0; i < bufferSize; i++) {
    if (buffer[i] < 0x10) uidStr += "0";
    uidStr += String(buffer[i], HEX);
  }
  uidStr.toUpperCase();
  return uidStr;
}

void addHistoryLog(String uid, String name, String status) {
  String timeNow = getLocalTimeStr();
  HistoryLog newLog = {timeNow, uid, name, status};
  accessLogs.insert(accessLogs.begin(), newLog);
  if (accessLogs.size() > 10) accessLogs.pop_back();
}

void guiEmailTask(String subject, String body) {
  emailMsg.clear();
  emailMsg.sender.name = "ESP32 System";
  emailMsg.sender.email = EMAIL_GUI;
  emailMsg.subject = subject;
  emailMsg.addRecipient("Admin", EMAIL_NHAN);
  emailMsg.text.content = body;
  emailMsg.text.charSet = "utf-8";

  if (!smtp.connect(&sessionConfig)) return;
  if (!MailClient.sendMail(&smtp, &emailMsg)) {
    Serial.println("Loi gui mail");
  } else {
    Serial.println("Email gui thanh cong");
  }
}

// === HÀM ĐIỀU KHIỂN CỬA (Non-blocking) ===
void openDoor() {
  if (!isDoorOpen) {
    doorServo.write(DOOR_OPEN_ANGLE);
    isDoorOpen = true;
    doorTimer = millis();
    Serial.println("-> Đã mở cửa");
  } else {
     doorTimer = millis(); 
  }
}

void closeDoor() {
  if (isDoorOpen) {
    doorServo.write(DOOR_CLOSE_ANGLE);
    isDoorOpen = false;
    Serial.println("-> Đã đóng cửa tự động");
  }
}

// --- HTML DATA GENERATORS ---
String getHistoryHTML() {
  String html = "";
  if (accessLogs.empty()) return "<tr><td colspan='4' style='text-align:center'>Chưa có lịch sử</td></tr>";
  for (const auto& log : accessLogs) {
    String color = (log.status == "OK") ? "green" : "red";
    html += "<tr><td>" + log.time + "</td><td><strong>" + log.name + "</strong></td><td>" + log.uid + "</td><td style='color:" + color + "'>" + log.status + "</td></tr>";
  }
  return html;
}

String getListUsersHTML() {
  String html = "";
  nvs_handle_t my_handle;
  esp_err_t err = nvs_open("users", NVS_READONLY, &my_handle);
  if (err != ESP_OK) return "<tr><td colspan='3'>Chưa có dữ liệu</td></tr>";

  nvs_iterator_t it = NULL;
  esp_err_t res = nvs_entry_find("nvs", "users", NVS_TYPE_STR, &it);
  while (res == ESP_OK) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    size_t required_size;
    nvs_get_str(my_handle, info.key, NULL, &required_size);
    char* name_buff = (char*)malloc(required_size);
    nvs_get_str(my_handle, info.key, name_buff, &required_size);
    
    html += "<tr><td>" + String(info.key) + "</td><td><strong>" + String(name_buff) + "</strong></td><td><a href='/delete?uid=" + String(info.key) + "' class='btn-delete'>Xóa</a></td></tr>";
    
    free(name_buff);
    res = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  nvs_close(my_handle);
  return html;
}

// --- API ENDPOINTS (AJAX) ---
void handleDataHistory() { server.send(200, "text/plain", getHistoryHTML()); }
void handleDataUsers() { server.send(200, "text/plain", getListUsersHTML()); }
void handleDataStatus() {
  String json = "{";
  json += "\"uid\":\"" + lastUid + "\",";
  json += "\"name\":\"" + (lastName=="" ? "Chưa đăng ký" : lastName) + "\"";
  json += "}";
  server.send(200, "application/json", json);
}

// --- GIAO DIỆN WEB ---
void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="vi">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 Access Control</title>
  <style>
    :root { --primary: #4361ee; --danger: #ef233c; --success: #2ec4b6; --bg: #f4f7fa; --card: #ffffff; }
    body { font-family: 'Segoe UI', sans-serif; background: var(--bg); margin: 0; padding: 20px; color: #333; }
    .container { max-width: 1200px; margin: auto; display: grid; grid-template-columns: 1fr 1fr; gap: 20px; }
    .card { background: var(--card); padding: 20px; border-radius: 12px; box-shadow: 0 2px 10px rgba(0,0,0,0.05); }
    h2 { text-align: center; color: var(--primary); }
    h3 { border-bottom: 2px solid #eee; padding-bottom: 10px; color: #555; }
    
    input[type=text] { width: 100%; padding: 10px; margin: 10px 0; border: 1px solid #ddd; border-radius: 6px; box-sizing: border-box; }
    button { border: none; padding: 10px; border-radius: 6px; cursor: pointer; color: white; font-weight: bold; width: 100%; }
    .btn-save { background: var(--success); }
    .btn-delete { background: var(--danger); width: auto; font-size: 0.8rem; padding: 5px 10px; }
    
    table { width: 100%; border-collapse: collapse; margin-top: 10px; font-size: 0.9rem; }
    th, td { text-align: left; padding: 10px; border-bottom: 1px solid #eee; }
    .uid-badge { background: #e0f2f1; color: #00695c; padding: 5px 10px; border-radius: 4px; font-weight: bold; display: inline-block; margin-bottom: 10px;}
    
    @media (max-width: 800px) { .container { grid-template-columns: 1fr; } }
  </style>
  <script>
    // Hàm cập nhật dữ liệu tự động không cần reload trang
    setInterval(() => {
        fetch('/data/history').then(r => r.text()).then(html => {
            document.getElementById('history_body').innerHTML = html;
        });
        fetch('/data/users').then(r => r.text()).then(html => {
            document.getElementById('user_body').innerHTML = html;
        });
        fetch('/data/status').then(r => r.json()).then(data => {
            if (data.uid != "") {
                document.getElementById('scan_msg').style.display = 'none';
                document.getElementById('scan_info').style.display = 'block';
                document.getElementById('uid_display').innerText = "UID: " + data.uid;
                document.getElementById('name_display').innerHTML = "Tên: <strong>" + data.name + "</strong>";
                document.getElementById('input_uid').value = data.uid;
            } else {
                document.getElementById('scan_msg').style.display = 'block';
                document.getElementById('scan_info').style.display = 'none';
            }
        });
    }, 2000);
  </script>
</head>
<body>
  <h2>HỆ THỐNG CHẤM CÔNG RFID</h2>
  <div class="container">
    <div>
      <div class="card">
        <h3>TRẠM XỬ LÝ</h3>
        <div id="scan_msg" style="text-align:center; color:#999"><i>Hãy quẹt thẻ...</i></div>
        <div id="scan_info" style="display:none">
            <div style="text-align:center">
                <div class="uid-badge" id="uid_display">UID: ...</div>
                <div id="name_display">Tên: ...</div>
            </div>
            <form action="/save" method="GET" style="margin-top:15px">
                <input type="hidden" id="input_uid" name="uid" value="">
                <input type="text" name="name" placeholder="Nhập tên..." required>
                <button type="submit" class="btn-save">LƯU TÊN</button>
            </form>
        </div>
      </div>
      <div class="card" style="margin-top: 20px;">
        <h3>LỊCH SỬ RA VÀO</h3>
        <table><thead><tr><th>Giờ</th><th>Tên</th><th>UID</th><th>TT</th></tr></thead><tbody id="history_body"></tbody></table>
      </div>
    </div>
    <div class="card">
      <h3>DANH SÁCH ĐÃ LƯU</h3>
      <table><thead><tr><th>UID</th><th>Tên</th><th>Xóa</th></tr></thead><tbody id="user_body"></tbody></table>
    </div>
  </div>
</body></html>)rawliteral";
  server.send(200, "text/html", html);
}

void handleSave() {
  if (server.hasArg("uid") && server.hasArg("name")) {
    String uid = server.arg("uid");
    String name = server.arg("name");
    preferences.begin("users", false);
    preferences.putString(uid.c_str(), name);
    preferences.end();
    lastName = name;
    guiEmailTask("CẬP NHẬT", "Đã cập nhật: " + name + " (" + uid + ")");
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleDelete() {
  if (server.hasArg("uid")) {
    String uid = server.arg("uid");
    preferences.begin("users", false);
    preferences.remove(uid.c_str());
    preferences.end();
    if (uid == lastUid) lastName = "";
    guiEmailTask("ĐÃ XÓA", "Đã xóa thẻ: " + uid);
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void setup() {
  Serial.begin(115200);
  
  // Cài đặt Servo
  doorServo.setPeriodHertz(50); 
  doorServo.attach(SERVO_PIN); 
  doorServo.write(DOOR_CLOSE_ANGLE); 
  
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nIP Web: " + WiFi.localIP().toString());

  // <<< FIX LỖI KHAI BÁO configTime >>>
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  sessionConfig.server.host_name = "smtp.gmail.com";
  sessionConfig.server.port = 465;
  sessionConfig.login.email = EMAIL_GUI;
  sessionConfig.login.password = EMAIL_MATKHAU;
  // <<< Vô hiệu hóa NTP trùng lặp trong sessionConfig để tránh lỗi (Giữ nguyên gmtOffset_sec) >>>
  sessionConfig.time.ntp_server = ""; 
  sessionConfig.time.gmt_offset = gmtOffset_sec; 

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, SS_PIN);
  rfid.PCD_Init();
  
  preferences.begin("users", false);
  preferences.end();

  server.on("/", handleRoot);
  server.on("/save", handleSave);
  server.on("/delete", handleDelete);
  server.on("/data/history", handleDataHistory);
  server.on("/data/users", handleDataUsers);
  server.on("/data/status", handleDataStatus);

  server.begin();
  
  guiEmailTask("HỆ THỐNG TRỰC TUYẾN", "Giao diện Web: http://" + WiFi.localIP().toString());
}

void loop() {
  server.handleClient();

  // --- KIỂM TRA TỰ ĐỘNG ĐÓNG CỬA ---
  if (isDoorOpen && (millis() - doorTimer > DOOR_DELAY_MS)) {
    closeDoor();
  }

  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  String uid = uidToHex(rfid.uid.uidByte, rfid.uid.size);
  
  if (uid == lastUid && millis() - lastTimeMillis < 2000) {
    rfid.PICC_HaltA(); return;
  }
  lastUid = uid;
  lastTimeMillis = millis();

  preferences.begin("users", true);
  String name = preferences.getString(uid.c_str(), "");
  preferences.end();
  lastName = name;
  
  Serial.print("Quet the: " + uid);
  if (name != "") {
    Serial.println(" -> " + name + " (OK)");
    openDoor(); 
    addHistoryLog(uid, name, "OK"); 
    guiEmailTask("NHẬT KÝ: Check-in", "Nhân viên: " + name + "\nThời gian: " + getLocalTimeStr());
  } else {
    Serial.println(" -> UNKNOWN (DENIED)");
    if(isDoorOpen) closeDoor(); 
    addHistoryLog(uid, "Thẻ lạ", "UNKNOWN");
    guiEmailTask("CẢNH BÁO", "Phát hiện thẻ lạ: " + uid + "\nThời gian: " + getLocalTimeStr());
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}