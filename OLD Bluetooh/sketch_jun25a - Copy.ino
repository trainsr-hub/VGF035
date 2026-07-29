// path: sketch_jun25a.ino
#include <Arduino.h>
#include <SPI.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

const char* LOCAL_SSID = "Phong tro 222";
const char* LOCAL_PASS = "158a32222";

WiFiServer tuningServer(8080);
WiFiClient tuningClient;

#include "src/config.h"
#include "src/rfid_logic/RfidScanner.h"
#include "src/audio_logic/Jq8900Player.h"
#include "src/sensor_logic/LineSensor.h"
#include "src/sensor_logic/UltrasonicSensor.h"
#include "src/pid_logic/PidController.h"
#include "src/stepper_logic/StepperMotor.h"
#include "src/gamepad_logic/GamepadController.h"

Preferences preferences;

enum SystemMode { MODE_GAMEPAD, MODE_PHONE_TUNING };
SystemMode currentMode;

bool isStopped = true;
bool isOTAUpdating = false;

bool OTA_ON = true;

float Kp = 1.2;
float Ki = 0.0;
float Kd = 0.6;
unsigned long lostLineTimeout = 10000;
String routePlan = "";

const int STEPPER_MAX_POS = 3 * STEPS_PER_ROTATION;

int telemetry_PWM_L = 0;
int telemetry_PWM_R = 0;
String telemetry_MotL_State = "STOP";
String telemetry_MotR_State = "STOP";
AGVState agvState = ST_TRACKING;

float currentDistance = 999.0;
unsigned long lostLineStartTime = 0;
unsigned long lastDebugTime = 0;
int rfidStep = 0; 
String lastRfidID = "NONE";

bool hasPlayedSound3 = false;
bool hasPlayedSound7 = false;
bool obstacleSoundPlayed = false;

String currentAction = "DUNG_YEN"; 

/* =========================================================================
   BLACK BOX: MOTOR CONTROLLER
========================================================================= */
void brakeMotor() {
    digitalWrite(IN1_L, HIGH); digitalWrite(IN2_L, HIGH); ledcWrite(CH_L, MAX_PWM_VAL);
    digitalWrite(IN1_R, HIGH); digitalWrite(IN2_R, HIGH); ledcWrite(CH_R, MAX_PWM_VAL);
    telemetry_PWM_L = MAX_PWM_VAL; telemetry_PWM_R = MAX_PWM_VAL;
    telemetry_MotL_State = "BRAKE"; telemetry_MotR_State = "BRAKE";
}

void driveMotor(int pwmLeft, int pwmRight) {
    if (pwmLeft >= 0) {
        digitalWrite(IN1_L, HIGH); digitalWrite(IN2_L, LOW); ledcWrite(CH_L, pwmLeft);
        telemetry_MotL_State = "FORWARD"; telemetry_PWM_L = pwmLeft;
    } else {
        digitalWrite(IN1_L, LOW); digitalWrite(IN2_L, HIGH); ledcWrite(CH_L, -pwmLeft);
        telemetry_MotL_State = "REVERSE"; telemetry_PWM_L = -pwmLeft;
    }

    if (pwmRight >= 0) {
        digitalWrite(IN1_R, HIGH); digitalWrite(IN2_R, LOW); ledcWrite(CH_R, pwmRight);
        telemetry_MotR_State = "FORWARD"; telemetry_PWM_R = pwmRight;
    } else {
        digitalWrite(IN1_R, LOW); digitalWrite(IN2_R, HIGH); ledcWrite(CH_R, -pwmRight);
        telemetry_MotR_State = "REVERSE"; telemetry_PWM_R = -pwmRight;
    }
}

/* =========================================================================
   BLACK BOX: SYSTEM INITIALIZATION (MODE TUNING)
========================================================================= */
void initTuningMode() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(LOCAL_SSID, LOCAL_PASS);
    Serial.print(">> Đang kết nối Local WiFi...");
    
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500); Serial.print("."); attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n>> [THÀNH CÔNG] Đã vào Local WiFi.");
        Serial.print(">> IP OTA & Tuning: "); Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n>> [THẤT BẠI] Không tìm thấy Local WiFi. Chuyển sang AP Mode.");
        WiFi.mode(WIFI_AP); WiFi.softAP("VARIS_AGV", "12345678"); 
        Serial.println(">> Bật WiFi AP: IP 192.168.4.1 | Port: 8080");
    }

    SPI.begin(RC522_SCK, RC522_MISO, RC522_MOSI, -1); 
    rfid_init();

    preferences.begin("agv_config", false);
    Kp = preferences.getFloat("Kp", 1.2);
    Ki = preferences.getFloat("Ki", 0.0);
    Kd = preferences.getFloat("Kd", 0.6);
    lostLineTimeout = preferences.getULong("timeout", 10000);
    routePlan = preferences.getString("route", "");
    OTA_ON = preferences.getBool("OTA_ON", true);

    ArduinoOTA.setHostname("VARIS_AGV_WIFI");
    ArduinoOTA.setPassword("2222");

    ArduinoOTA.onStart([]() {
        isOTAUpdating = true; 
        esp_task_wdt_delete(NULL); 
        brakeMotor(); 
        Serial.println("\n>> [OTA] BẮT ĐẦU NẠP CODE QUA WIFI...");
    });
    ArduinoOTA.onEnd([]() { Serial.println("\n>> [OTA] NẠP HOÀN TẤT! ĐANG KHỞI ĐỘNG LẠI..."); });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) { Serial.printf(">> [OTA] Tiến độ: %u%%\r", (progress / (total / 100))); });
    ArduinoOTA.onError([](ota_error_t error) {
        isOTAUpdating = false; 
        Serial.printf("\n>> [OTA] LỖI [%u]\n", error);
    });

    ArduinoOTA.begin();
    tuningServer.begin();
}

/* =========================================================================
   BLACK BOX: GAMEPAD PROCESSOR
========================================================================= */
void processGamepadMode() {
    gamepad_update();
    GamepadData pad = gamepad_get_data();
    
    if (pad.justConnected) { brakeMotor(); isStopped = true; audio_play_track(5); }
    if (pad.justDisconnected) { brakeMotor(); isStopped = true; audio_play_track(6); }

    if (!pad.isConnected) { brakeMotor(); return; }

    static GamepadData lastPad = {0};
    if (pad.btnA && !lastPad.btnA) { audio_play_track(2); }
    if (pad.btnY && !lastPad.btnY) {
        if (stepper_is_reached()) {
            stepper_set_target(STEPPER_MAX_POS); currentAction = "QUAY_RA (3 VONG)";
        } else {
            stepper_set_target(0); currentAction = "THU_VE (VI TRI 0)";
        }
    }
    
    if (pad.up) { driveMotor(DRIVE_MAX_PWM, DRIVE_MAX_PWM); isStopped = false; if (stepper_is_reached()) currentAction = "BT_TIEN"; } 
    else if (pad.down) { driveMotor(-DRIVE_MAX_PWM, -DRIVE_MAX_PWM); isStopped = false; if (stepper_is_reached()) currentAction = "BT_LUI"; } 
    else if (pad.left || pad.btnX) { driveMotor(-DRIVE_MAX_PWM, DRIVE_MAX_PWM); isStopped = false; if (stepper_is_reached()) currentAction = "BT_XOAY_TRAI"; } 
    else if (pad.right || pad.btnB) { driveMotor(DRIVE_MAX_PWM, -DRIVE_MAX_PWM); isStopped = false; if (stepper_is_reached()) currentAction = "BT_XOAY_PHAI"; } 
    else {
        if (!isStopped) { brakeMotor(); isStopped = true; if (stepper_is_reached()) currentAction = "BT_DUNG"; }
    }
    lastPad = pad;
}

/* =========================================================================
   BLACK BOX: TUNING COMMAND PROCESSOR
   [HIGHLIGHT THAY ĐỔI]: 
   - Bổ sung logic bắt sự kiện Connect/Disconnect của Client vào TCP Port 8080.
   - Sử dụng biến static `wasConnected` để so sánh trạng thái trước/sau, tránh 
     gọi phát nhạc liên tục trong hàm loop.
========================================================================= */
void processTuningCommands() {
    static bool wasConnected = false;

    // Quét tìm client mới nếu hiện tại chưa có ai kết nối
    if (!tuningClient || !tuningClient.connected()) {
        tuningClient = tuningServer.available();
    }
    
    // Đánh giá trạng thái kết nối ngay thời điểm hiện tại
    bool isConnected = tuningClient && tuningClient.connected();

    // Logic chốt chặn (Edge Detection): So sánh hiện tại với quá khứ
    if (isConnected && !wasConnected) {
        audio_play_track(5);
        Serial.println(">> [SỰ KIỆN] Phone đã KẾT NỐI vào Port 8080!");
    } 
    else if (!isConnected && wasConnected) {
        audio_play_track(6);
        Serial.println(">> [SỰ KIỆN] Phone đã NGẮT KẾT NỐI khỏi Port 8080!");
    }
    
    // Cập nhật lại lịch sử trạng thái cho vòng lặp tiếp theo
    wasConnected = isConnected;

    // Xử lý dữ liệu nạp vào nếu client đang active
    if (isConnected && tuningClient.available()) {
        String cmd = tuningClient.readStringUntil('\n');
        cmd.trim(); cmd.toUpperCase();
        
        if (cmd == "SAVE") {
            preferences.putFloat("Kp", Kp); preferences.putFloat("Ki", Ki); preferences.putFloat("Kd", Kd);
            preferences.putULong("timeout", lostLineTimeout); preferences.putString("route", routePlan);
            preferences.putBool("OTA_ON", OTA_ON);
            tuningClient.println(">> ĐÃ LƯU TẤT CẢ THÔNG SỐ VÀO NVS (FLASH) THÀNH CÔNG!");
        } 
        else if (cmd.startsWith("P")) { Kp = cmd.substring(1).toFloat(); tuningClient.println(">> SET Kp = " + String(Kp)); }
        else if (cmd.startsWith("I")) { Ki = cmd.substring(1).toFloat(); tuningClient.println(">> SET Ki = " + String(Ki)); }
        else if (cmd.startsWith("D")) { Kd = cmd.substring(1).toFloat(); tuningClient.println(">> SET Kd = " + String(Kd)); }
        else if (cmd.startsWith("T")) { lostLineTimeout = cmd.substring(1).toInt(); tuningClient.println(">> SET TIMEOUT = " + String(lostLineTimeout) + "ms"); }
        else if (cmd.startsWith("G")) { routePlan = cmd; tuningClient.println(">> SET ROUTE = " + routePlan); }
        else if (cmd == "OTA0") { OTA_ON = false; preferences.putBool("OTA_ON", false); tuningClient.println(">> ĐÃ TẮT OTA"); }
        else if (cmd == "OTA1") { OTA_ON = true; preferences.putBool("OTA_ON", true); tuningClient.println(">> ĐÃ BẬT OTA"); }
        else { tuningClient.println(">> LỆNH KHÔNG HỢP LỆ. Dùng: Px, Ix, Dx, Tx, Gx, OTA0, OTA1 hoặc SAVE"); }
    }
}
/* ========================================================================= */
// [Sửa đổi tập trung tại khối BLACK BOX: TUNING COMMAND PROCESSOR]
// - Khởi tạo một cờ tĩnh (static bool wasConnected).
// - Nếu client mới kết nối `isConnected == true` nhưng `wasConnected == false` 
//   thì phát track 5 (Tương tự logic Gamepad connect).
// - Nếu client rớt `isConnected == false` nhưng `wasConnected == true` thì phát 
//   track 6.
// - Cuối cùng, luôn đồng bộ wasConnected = isConnected.
/* ========================================================================= */

/* =========================================================================
   BLACK BOX: RFID ACTION PROCESSOR
========================================================================= */
void handleRFIDAction(String route, String rfid) {
    currentAction = "DOC_THE_RFID: " + rfid;
    switch(rfidStep) {
        case 0: 
            if (telemetry_MotL_State == "BRAKE" && telemetry_MotR_State == "BRAKE") {
                audio_play_track(4); stepper_set_target(STEPPER_MAX_POS); rfidStep = 1;
            }
            break;
        case 1: 
            if (stepper_is_reached()) { stepper_set_target(0); rfidStep = 2; }
            break;
        case 2: 
            if (stepper_is_reached()) { 
                reset_pid(); 
                lastRfidID = "NONE"; 
            }
            break;
    }
}

/* =========================================================================
   BLACK BOX: PID & SENSOR TRACKING
========================================================================= */
void processPIDTracking() {
    uint8_t lineData = line_sensor_read();
    bool isLost = (lineData == 0x1F || lineData == 0x00);
    float speedFactor = 1.0;
    
    currentDistance = ultrasonic_get_distance();
    if (currentDistance <= 15.0) { speedFactor = 0.0; } 
    else if (currentDistance <= 35.0) { speedFactor = (currentDistance - 15.0) / 20.0; }

    if (speedFactor == 0.0) {
        brakeMotor(); currentAction = "VAT_CAN_DUNG_HAN";
        if (!obstacleSoundPlayed) { audio_play_track(2); obstacleSoundPlayed = true; }
        return;
    }

    if (isLost) {
        if (agvState != ST_ERROR_LOST_LINE) {
            agvState = ST_ERROR_LOST_LINE; lostLineStartTime = millis();
            hasPlayedSound3 = false; hasPlayedSound7 = false; currentAction = "MAT_LINE_CHO_QUAN_TINH";
        }
    }

    if (agvState == ST_ERROR_LOST_LINE) {
        if (!isLost) {
            audio_stop(); hasPlayedSound3 = false; hasPlayedSound7 = false; agvState = ST_TRACKING;
        } else {
            unsigned long lostDuration = millis() - lostLineStartTime;
            if (lostDuration <= 1000) {
                MotorSpeed target = get_last_inertial_speed(speedFactor);
                driveMotor(target.left, target.right); currentAction = "QUAN_TINH_MAT_LINE";
            } else {
                brakeMotor(); reset_pid(); currentAction = "MAT_LINE_DUNG_HAN";
                if (lostDuration >= (lostLineTimeout + 20000)) { digitalWrite(STBY, LOW); esp_deep_sleep_start(); } 
                else if (lostDuration >= (lostLineTimeout + 15000)) { if (!hasPlayedSound7) { audio_play_track(7); hasPlayedSound7 = true; } } 
                else if (lostDuration >= lostLineTimeout) { if (!hasPlayedSound3) { audio_play_track(3); hasPlayedSound3 = true; } }
            }
        }
    } else {
        if (speedFactor < 1.0) {
            currentAction = "VAT_CAN_GIAM_TOC";
            if (!obstacleSoundPlayed) { audio_play_track(2); obstacleSoundPlayed = true; }
        } else {
            currentAction = "PID_BAM_LINE"; obstacleSoundPlayed = false;
        }
        MotorSpeed target = calculate_motor_speed(lineData, Kp, Ki, Kd, speedFactor);
        driveMotor(target.left, target.right);
    }
}

void setup() {
    Serial.begin(115200);
    audio_init();
    line_sensor_init();
    ultrasonic_init();
    stepper_init();
    delay(100); 

    uint8_t bootLineData = line_sensor_read();
    if (bootLineData == 0x1F) {
        currentMode = MODE_GAMEPAD;
        gamepad_init();
        Serial.println(">> CHẾ ĐỘ: GAMEPAD");
    } else {
        currentMode = MODE_PHONE_TUNING;
        Serial.println(">> CHẾ ĐỘ: PHONE TUNING");
        initTuningMode();
    }

    pinMode(STBY, OUTPUT);
    pinMode(IN1_L, OUTPUT); pinMode(IN2_L, OUTPUT);
    pinMode(IN1_R, OUTPUT); pinMode(IN2_R, OUTPUT);
    digitalWrite(STBY, HIGH);
    ledcSetup(CH_L, PWM_FREQ, PWM_RES); ledcSetup(CH_R, PWM_FREQ, PWM_RES); 
    ledcAttachPin(PWM_L, CH_L); ledcAttachPin(PWM_R, CH_R); 

    audio_play_track(1); 
    delay(2000);
    
    esp_task_wdt_init(WDT_TIMEOUT, true);
    esp_task_wdt_add(NULL); 
}

void loop() {
    esp_task_wdt_reset();

    if (currentMode == MODE_GAMEPAD) {
        processGamepadMode();
    } 
    else if (currentMode == MODE_PHONE_TUNING) {
        if (OTA_ON) { ArduinoOTA.handle(); }

        if (!isOTAUpdating) {
            String scannedID = rfid_scan();
            if (scannedID != "" && lastRfidID == "NONE") { 
                lastRfidID = scannedID; rfidStep = 0; brakeMotor(); 
            }

            if (lastRfidID != "NONE") {
                handleRFIDAction(routePlan, lastRfidID);
            } else {
                processPIDTracking();
            }
            processTuningCommands();
        }
    }

    stepper_run_non_blocking();
    
    if (millis() - lastDebugTime >= 3000 && !isOTAUpdating) {
        lastDebugTime = millis();
        String report = "\n--- [BÁO CÁO HỆ THỐNG] ---\n";
        report += "HÀNH ĐỘNG XE: " + currentAction + "\n";
        
        char pidBuffer[100];
        sprintf(pidBuffer, "THÔNG SỐ PID: P=%.2f, I=%.2f, D=%.2f | TỰ THÍCH NGHI: %.2f\n", Kp, Ki, Kd, get_auto_adaptive_gain());
        report += String(pidBuffer);
        
        report += "MẤT LINE TIMEOUT: " + String(lostLineTimeout) + "ms | ROUTE CHỜ: " + (routePlan == "" ? "[TRỐNG]" : routePlan) + "\n";
        report += "OTA_ON STATE: " + String(OTA_ON ? "TRUE (BẬT)" : "FALSE (TẮT)") + "\n";
        report += "MOTOR TRÁI  | Trạng thái: " + telemetry_MotL_State + " | Xung: " + String(telemetry_PWM_L) + "\n";
        report += "MOTOR PHẢI  | Trạng thái: " + telemetry_MotR_State + " | Xung: " + String(telemetry_PWM_R) + "\n";
        
        if (currentMode == MODE_PHONE_TUNING) {
            report += "KHOẢNG CÁCH: " + String(currentDistance) + " cm\n";
            report += "RFID: " + String(rfid_check_health() ? "ONLINE - " + lastRfidID : "OFFLINE") + "\n";
        } else {
            report += "GAMEPAD MODE: MỌI CẢM BIẾN BỊ TẮT\n";
        }
        report += "-----------------------------\n";

        Serial.print(report);
        if (currentMode == MODE_PHONE_TUNING && tuningClient && tuningClient.connected()) tuningClient.print(report);
    }
}