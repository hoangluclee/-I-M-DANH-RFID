
#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <ESP_Mail_Client.h>

// ====== CẤU HÌNH WIFI ======
const char* WIFI_SSID = "Luc";
const char* WIFI_PASS = "12345678";

// ====== CẤU HÌNH EMAIL ======
#define EMAIL_GUI      "lehoangluc246@gmail.com"
#define EMAIL_MATKHAU  "qroldjdgxauhiexf"  
#define EMAIL_NHAN     "lehoangluc.cv@gmail.com"

// ====== CHÂN CẢM BIẾN & SERVO ======
#define TRIG_PIN 26
#define ECHO_PIN 25
#define SERVO_PIN 27

// ====== CẤU HÌNH SERVO ======
#define DOOR_OPEN_ANGLE  120
#define DOOR_CLOSE_ANGLE 0

// ====== NGƯỠNG PHÁT HIỆN ======
#define NGUONG_CM 80
#define TIME_BETWEEN_DETECT 1200

// ====== BIẾN TOÀN CỤC ======
WebServer server(80);
Servo doorServo;

unsigned long lastDetect = 0;
long lastDistance = 500;
unsigned long soNguoiVao = 0;
unsigned long soNguoiRa  = 0;

// ====== SMTP ======
SMTPSession smtp;
ESP_Mail_Session sessionConfig;
SMTP_Message emailMsg;

// ===========================================================
//              HÀM GỬI EMAIL
// ===========================================================
void guiEmail(const char* tieude, const char* noidung) {
  Serial.println("Dang gui email...");

  emailMsg.clear();
  emailMsg.sender.name = "He thong ESP32";
  emailMsg.sender.email = EMAIL_GUI;
  emailMsg.subject = tieude;
  emailMsg.addRecipient("Nguoi dung", EMAIL_NHAN);

  emailMsg.text.content = noidung;
  emailMsg.text.charSet = "utf-8";
  emailMsg.text.transfer_encoding = Content_Transfer_Encoding::enc_7bit;

  if (!smtp.connect(&sessionConfig)) {
    Serial.println("Khong ket noi duoc SMTP!");
    return;
  }

  if (!MailClient.sendMail(&smtp, &emailMsg)) {
    Serial.print("Loi gui mail: ");
    Serial.println(smtp.errorReason());
  } else {
    Serial.println("Email da gui!");
  }

  smtp.closeSession();
}

// ===========================================================
//              ĐỌC CẢM BIẾN HC-SR04
// ===========================================================
long docKhoangCach() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  unsigned long duration = pulseIn(ECHO_PIN, HIGH, 30000);
  if (duration == 0) return 9999;

  return duration * 0.0343 / 2;
}

// ===========================================================
//              XỬ LÝ PHÁT HIỆN NGƯỜI VÀO/RA
// ===========================================================
void xuLyPhatHien(long dist) {
  unsigned long now = millis();

  if (dist < NGUONG_CM) {
    if (now - lastDetect < TIME_BETWEEN_DETECT) return;
    lastDetect = now;

    if (dist < lastDistance) {
      soNguoiVao++;
      Serial.println("Phat hien nguoi vao!");
      guiEmail("Canh bao: Co nguoi vao", 
               "He thong ESP32 phat hien co nguoi vua di vao khu vuc.");
    } else {
      soNguoiRa++;
      Serial.println("Phat hien nguoi ra!");
      guiEmail("Canh bao: Co nguoi ra", 
               "He thong ESP32 phat hien co nguoi vua di ra khoi khu vuc.");
    }
  }

  lastDistance = dist;
}

// ===========================================================
//              HÀM ĐIỀU KHIỂN CỬA SERVO
// ===========================================================
void moCua() {
  doorServo.write(DOOR_OPEN_ANGLE);
  Serial.println("Cua da mo (khong gui email).");
}

void dongCua() {
  doorServo.write(DOOR_CLOSE_ANGLE);
  Serial.println("Cua da dong (khong gui email).");
}

// ===========================================================
//              GIAO DIỆN WEB
// ===========================================================
void handleRoot() {
  String html = "<h2>ESP32 Door + Counter System</h2>";
  html += "<p>So nguoi vao: " + String(soNguoiVao) + "</p>";
  html += "<p>So nguoi ra: " + String(soNguoiRa) + "</p>";
  html += "<button onclick=\"fetch('/open')\">Mo cua</button>";
  html += "<button onclick=\"fetch('/close')\">Dong cua</button>";
  server.send(200, "text/html", html);
}

void handleOpen() {
  moCua();
  server.send(200, "text/plain", "Cua da mo");
}

void handleClose() {
  dongCua();
  server.send(200, "text/plain", "Cua da dong");
}

void handleStatus() {
  server.send(200, "application/json",
               "{\"vao\":" + String(soNguoiVao) +
               ",\"ra\":" + String(soNguoiRa) + "}");
}

// ===========================================================
//                      SETUP
// ===========================================================
void setup() {
  Serial.begin(115200);
  delay(300);

  // Pin
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  // Servo
  doorServo.setPeriodHertz(50);
  doorServo.attach(SERVO_PIN, 500, 2400);
  doorServo.write(DOOR_CLOSE_ANGLE);

  // WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Dang ket noi WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nDa ket noi WiFi!");
  Serial.println(WiFi.localIP());

  // Web Server
  server.on("/", handleRoot);
  server.on("/open", handleOpen);
  server.on("/close", handleClose);
  server.on("/status", handleStatus);
  server.begin();

  // SMTP Config
  sessionConfig.server.host_name = "smtp.gmail.com";
  sessionConfig.server.port = 465;
  sessionConfig.login.email = EMAIL_GUI;
  sessionConfig.login.password = EMAIL_MATKHAU;
  sessionConfig.login.user_domain = "";
  sessionConfig.time.ntp_server = "pool.ntp.org";
  sessionConfig.time.gmt_offset = 7 * 3600;
  sessionConfig.time.day_light_offset = 0;

  Serial.println("SMTP san sang.");
}

// ===========================================================
//                      LOOP
// ===========================================================
void loop() {
  server.handleClient();

  long kc = docKhoangCach();
  Serial.printf("Khoang cach: %ld cm\n", kc);
  xuLyPhatHien(kc);

  delay(200);
}
