// path: VGF035.ino
#include <Arduino.h>
#include <SPI.h>
#include <esp_task_wdt.h>
#include <Preferences.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>

/* =========================================================================
   [HIGHLIGHT THAY ĐỔI]: 
   - Xoá thư viện ArduinoOTA cục bộ.
   - Bổ sung các thư viện mạng và OTA cần thiết cho giao thức HTTPS.
========================================================================= */
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
/* ========================================================================= */

const char* LOCAL_SSID = "Phong tro 222";
const char* LOCAL_PASS = "158a32222";

/* =========================================================================
   [HIGHLIGHT THAY ĐỔI]: 
   - Biến lưu URL cố định để tải file .bin. 
   - YÊU CẦU: Thay thế <USERNAME> và <REPO_NAME> thành đúng thông tin GitHub của bạn.
========================================================================= */
String FIRMWARE_URL = "https://github.com/trainsr-hub/VGF035/releases/download/latest/sketch_jun25a.ino.bin";
/* ========================================================================= */

WiFiServer tuningServer(8080);
WiFiClient tuningClient;

#include "src/config.h"
#include "src/rfid_logic/RfidScanner.h"
#include "src/audio_logic/Jq8900Player.h"
#include "src/sensor_logic/LineSensor.h"
#include "src/sensor_logic/UltrasonicSensor.h"
#include "src/pid_logic/PidController.h"
#include "src/stepper_logic/StepperMotor.h"

const int MAX_PWM_VAL = (1 << PWM_RES) - 1;
int DRIVE_MAX_PWM = 200;
int BASE_PWM = 100;

Preferences preferences;

bool isStopped = true;
bool isOTAUpdating = false;
bool OTA_ON = true;
bool isAgvActive = false; 

bool isManeuvering = false;
String maneuverSequence = "";
int maneuverIndex = 0;
unsigned long maneuverStepEndTime = 0;
String activeRfidRole = "";

bool maneuverHasEscapedLine = false;

float Kp = 1.2;
float Ki = 0.0;
float Kd = 0.6;
unsigned long lostLineTimeout = 10000;
String routePlan = "G0G1G2";

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

String currentAction = "SLEEP_MODE"; 

String getRfidRole(String rfid) {
    if (rfid == "F5:53:E7:06") return "G0";
    if (rfid == "2E:D6:E6:06") return "G1";
    if (rfid == "83:73:E5:06") return "G2";
    if (rfid == "A1:7F:E5:06") return "G3";
    if (rfid == "06:3F:E7:06") return "G4";
    
    if (rfid == "72:BA:E6:06") return "STEPPER_UP_3";
    
    if (rfid == "43:EB:E7:06") return "SPARE_5";
    if (rfid == "5E:E8:E5:06") return "SPARE_6";
    if (rfid == "2A:0E:E6:06") return "SPARE_7";
    if (rfid == "BC:8E:FD:06") return "SPARE_X";
    
    return "UNKNOWN";
}

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
   [HIGHLIGHT THAY ĐỔI]: 
   - Thêm hàm performOTAUpdate() thực hiện kéo HTTPS độc lập. 
   - Đã xử lý ngắt tiến trình nguy hiểm (WDT, motor) trước khi update.
========================================================================= */
void performOTAUpdate() {
    if (WiFi.status() != WL_CONNECTED) {
        if (tuningClient) tuningClient.println(">> [OTA FAILED] Chưa kết nối WiFi, không thể OTA.");
        return;
    }

    isOTAUpdating = true;
    esp_task_wdt_delete(NULL); 
    brakeMotor(); 
    
    if (tuningClient) tuningClient.println(">> [OTA HTTPS] ĐANG KẾT NỐI TỚI GITHUB...");
    Serial.println(">> [OTA HTTPS] Bắt đầu quá trình tải Firmware từ GitHub...");

    WiFiClientSecure client;
    client.setInsecure(); // Bỏ qua kiểm tra chứng chỉ bảo mật của GitHub để chống lỗi hết hạn Root CA.
    
    // Yêu cầu HTTPUpdate phải follow redirect, do link tải Release của Github sẽ chuyển hướng sang AWS S3.
    httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    
    t_httpUpdate_return ret = httpUpdate.update(client, FIRMWARE_URL);
    
    switch (ret) {
        case HTTP_UPDATE_FAILED:
            if (tuningClient) tuningClient.printf(">> [OTA LỖI]: Cập nhật thất bại (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            Serial.printf("HTTP_UPDATE_FAILED Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            isOTAUpdating = false;
            break;
            
        case HTTP_UPDATE_NO_UPDATES:
            if (tuningClient) tuningClient.println(">> [OTA]: Không có bản cập nhật nào mới.");
            Serial.println("HTTP_UPDATE_NO_UPDATES");
            isOTAUpdating = false;
            break;
            
        case HTTP_UPDATE_OK:
            if (tuningClient) tuningClient.println(">> [OTA THÀNH CÔNG]: Sẽ tự động khởi động lại sau 1 giây...");
            Serial.println("HTTP_UPDATE_OK");
            delay(1000); // Đợi để gửi xong bản tin qua client
            ESP.restart();
            break;
    }
}
/* ========================================================================= */

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
    
    preferences.begin("agv_config", false);
    Kp = preferences.getFloat("Kp", 1.2);
    Ki = preferences.getFloat("Ki", 0.0);
    Kd = preferences.getFloat("Kd", 0.6);
    lostLineTimeout = preferences.getULong("timeout", 10000);
    routePlan = preferences.getString("route", "G0G1G2"); 
    OTA_ON = preferences.getBool("OTA_ON", true);

    DRIVE_MAX_PWM = preferences.getInt("max_pwm", 200);
    BASE_PWM = preferences.getInt("base_pwm", 100);

    /* =========================================================================
       [HIGHLIGHT THAY ĐỔI]: 
       - Đã xoá toàn bộ cài đặt ArduinoOTA cục bộ tại block này.
    ========================================================================= */
    tuningServer.begin();
}

void processTuningCommands() {
    static bool wasConnected = false;

    if (!tuningClient || !tuningClient.connected()) {
        tuningClient = tuningServer.available();
    }
    
    bool isConnected = tuningClient && tuningClient.connected();

    if (isConnected && !wasConnected) {
        audio_play_track(5);
        Serial.println(">> [SỰ KIỆN] Phone đã KẾT NỐI vào Port 8080!");
    } 
    else if (!isConnected && wasConnected) {
        audio_play_track(6);
        Serial.println(">> [SỰ KIỆN] Phone đã NGẮT KẾT NỐI khỏi Port 8080!");
    }
    
    wasConnected = isConnected;

    if (isConnected && tuningClient.available()) {
        String cmd = tuningClient.readStringUntil('\n');
        cmd.trim(); cmd.toUpperCase();
        
        bool isCmdManeuver = true;
        if (cmd.length() == 0) isCmdManeuver = false;
        for (int i = 0; i < cmd.length(); i++) {
            if (cmd[i] != 'F' && cmd[i] != 'R' && cmd[i] != 'L' && cmd[i] != 'X' && cmd[i] != 'Y') {
                isCmdManeuver = false; 
                break;
            }
        }
        
        if (cmd == "SAVE") {
            preferences.putFloat("Kp", Kp); preferences.putFloat("Ki", Ki); preferences.putFloat("Kd", Kd);
            preferences.putULong("timeout", lostLineTimeout); preferences.putString("route", routePlan);
            preferences.putBool("OTA_ON", OTA_ON);
            preferences.putInt("max_pwm", DRIVE_MAX_PWM);
            preferences.putInt("base_pwm", BASE_PWM);
            tuningClient.println(">> ĐÃ LƯU TẤT CẢ THÔNG SỐ VÀO NVS (FLASH) THÀNH CÔNG!");
        } 
        else if (cmd.startsWith("P")) { Kp = cmd.substring(1).toFloat(); tuningClient.println(">> SET Kp = " + String(Kp)); }
        else if (cmd.startsWith("I")) { Ki = cmd.substring(1).toFloat(); tuningClient.println(">> SET Ki = " + String(Ki)); }
        else if (cmd.startsWith("D")) { Kd = cmd.substring(1).toFloat(); tuningClient.println(">> SET Kd = " + String(Kd)); }
        else if (cmd.startsWith("T")) { lostLineTimeout = cmd.substring(1).toInt(); tuningClient.println(">> SET TIMEOUT = " + String(lostLineTimeout) + "ms"); }
        else if (cmd.startsWith("G")) { routePlan = cmd; tuningClient.println(">> SET ROUTE = " + routePlan); }
        else if (cmd == "OTA0") { OTA_ON = false; preferences.putBool("OTA_ON", false); tuningClient.println(">> ĐÃ TẮT CỜ CHO PHÉP OTA"); }
        else if (cmd == "OTA1") { OTA_ON = true; preferences.putBool("OTA_ON", true); tuningClient.println(">> ĐÃ BẬT CỜ CHO PHÉP OTA"); }
        
        /* =========================================================================
           [HIGHLIGHT THAY ĐỔI]: 
           - Bổ sung luồng xử lý lệnh "OTA UPDATE". 
           - Yêu cầu cờ OTA_ON phải đang bật thì mới thực thi.
        ========================================================================= */
        else if (cmd == "OTA UPDATE") {
            if (OTA_ON) {
                tuningClient.println(">> [COMMAND] ĐÃ NHẬN LỆNH OTA UPDATE. CHUẨN BỊ KÉO FIRMWARE MỚI...");
                performOTAUpdate();
            } else {
                tuningClient.println(">> [CẢNH BÁO] TÍNH NĂNG OTA ĐANG BỊ KHÓA, GỬI LỆNH 'OTA1' ĐỂ MỞ KHÓA.");
            }
        }
        /* ========================================================================= */

        else if (cmd.startsWith("MAX")) { 
            DRIVE_MAX_PWM = cmd.substring(3).toInt(); 
            tuningClient.println(">> SET DRIVE_MAX_PWM = " + String(DRIVE_MAX_PWM)); 
        }
        else if (cmd.startsWith("BAS")) { 
            BASE_PWM = cmd.substring(3).toInt(); 
            tuningClient.println(">> SET BASE_PWM = " + String(BASE_PWM)); 
        }
        
        else if (cmd == "AGV START") {
            isAgvActive = true;
            isManeuvering = false; 
            digitalWrite(STBY, HIGH);         
            digitalWrite(RC522_RST, HIGH);    
            delay(50);                        
            rfid_init();                      
            reset_pid(); 
            tuningClient.println(">> [AGV] ĐÃ WAKE UP HARDWARE (MOTOR & RFID). XE BẮT ĐẦU CHẠY!");
        }
        else if (cmd == "AGV STOP") {
            isAgvActive = false;
            isManeuvering = false;
            brakeMotor();
            digitalWrite(STBY, LOW);          
            digitalWrite(RC522_RST, LOW);     
            tuningClient.println(">> [AGV] ĐÃ NGẮT CẢM BIẾN VÀ ĐƯA MODULE VÀO SLEEP MODE!");
        }
        else if (cmd.startsWith("ST")) {
            int targetSteps = cmd.substring(2).toInt();
            stepper_set_target(targetSteps);
            tuningClient.println(">> [STEPPER] ĐANG DI CHUYỂN ĐẾN TỌA ĐỘ BƯỚC: " + String(targetSteps));
        }
        else if (isCmdManeuver) {
            isManeuvering = true;
            maneuverSequence = cmd;
            maneuverIndex = 0;
            maneuverStepEndTime = millis() + 1000;
            maneuverHasEscapedLine = false; 
            digitalWrite(STBY, HIGH); 
            reset_pid();
            tuningClient.println(">> [AGV] BẮT ĐẦU CHUỖI ĐIỀU HƯỚNG MỞ RỘNG (F/R/L/X/Y): " + cmd);
        }
        else { tuningClient.println(">> LỆNH KHÔNG HỢP LỆ. Dùng: P, I, D, T, G, OTA, MAX, BAS, OTA UPDATE, AGV START/STOP, ST[số], chuỗi [F/R/L/X/Y]"); }
    }
}

void handleRFIDAction(String role, String rfid) {
    currentAction = "DOC_THE_RFID: " + role;
    
    if (role.startsWith("G")) {
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
    else if (role == "STEPPER_UP_3") {
        switch(rfidStep) {
            case 0:
                audio_play_track(4); stepper_set_target(STEPPER_MAX_POS); rfidStep = 1;
                break;
            case 1:
                if (stepper_is_reached()) {
                    reset_pid(); 
                    lastRfidID = "NONE"; 
                }
                break;
        }
    }
}

void processPIDTracking(char bias = 'F') {
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
            currentAction = "PID_BAM_LINE_BIAS_" + String(bias); obstacleSoundPlayed = false;
        }
        MotorSpeed target = calculate_motor_speed(lineData, Kp, Ki, Kd, speedFactor, bias);
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

    Serial.println(">> CHẾ ĐỘ: KHÔNG DÂY (WIFI ONLY)");
    initTuningMode();

    pinMode(STBY, OUTPUT);
    pinMode(IN1_L, OUTPUT); pinMode(IN2_L, OUTPUT);
    pinMode(IN1_R, OUTPUT); pinMode(IN2_R, OUTPUT);
    
    ledcSetup(CH_L, PWM_FREQ, PWM_RES); ledcSetup(CH_R, PWM_FREQ, PWM_RES); 
    ledcAttachPin(PWM_L, CH_L); ledcAttachPin(PWM_R, CH_R); 

    pinMode(RC522_RST, OUTPUT);
    
    digitalWrite(STBY, LOW);       
    digitalWrite(RC522_RST, LOW);  

    audio_play_track(1); 
    delay(2000);
    
    esp_task_wdt_init(WDT_TIMEOUT, true);
    esp_task_wdt_add(NULL); 
}

void loop() {
    esp_task_wdt_reset();

    /* =========================================================================
       [HIGHLIGHT THAY ĐỔI]: 
       - Loại bỏ dòng ArduinoOTA.handle() khỏi loop. HTTPUpdate không cần liên tục lắng nghe.
    ========================================================================= */

    if (!isOTAUpdating) {
        
        if (isManeuvering) {
            char currentBias = maneuverSequence[maneuverIndex];
            bool stepCompleted = false;
            
            if (millis() >= maneuverStepEndTime) {
                stepCompleted = true; 
            } 
            else if (currentBias == 'X' || currentBias == 'Y') {
                uint8_t lineData = line_sensor_read();
                bool s2 = !((lineData >> 3) & 1);
                bool s3 = !((lineData >> 2) & 1);
                bool s4 = !((lineData >> 1) & 1);
                bool hasLine = (s2 || s3 || s4); 

                if (!maneuverHasEscapedLine) {
                    if (!hasLine) maneuverHasEscapedLine = true;
                } else {
                    if (hasLine) {
                        stepCompleted = true; 
                    }
                }
            }

            if (stepCompleted) {
                maneuverIndex++;
                maneuverHasEscapedLine = false; 
                
                if (maneuverIndex >= maneuverSequence.length()) {
                    isManeuvering = false;
                    brakeMotor();
                    if (!isAgvActive) digitalWrite(STBY, LOW);
                    reset_pid();
                } else {
                    maneuverStepEndTime = millis() + 1000;
                    currentBias = maneuverSequence[maneuverIndex]; 
                }
            }
            
            if (isManeuvering) {
                if (currentBias == 'F' || currentBias == 'R' || currentBias == 'L') {
                    processPIDTracking(currentBias);
                } else if (currentBias == 'X') {
                    driveMotor(DRIVE_MAX_PWM, -DRIVE_MAX_PWM);
                    currentAction = "QUAY_PHAI_TIM_LINE_MOI";
                } else if (currentBias == 'Y') {
                    driveMotor(-DRIVE_MAX_PWM, DRIVE_MAX_PWM);
                    currentAction = "QUAY_TRAI_TIM_LINE_MOI";
                }
            }
        } 
        else if (isAgvActive) {
            String scannedID = "";
            
            if (stepper_is_reached()) {
                scannedID = rfid_scan();
            }

            if (scannedID != "" && lastRfidID == "NONE") { 
                String role = getRfidRole(scannedID);
                
                if (role != "UNKNOWN") {
                    if ((role.startsWith("G") && routePlan.indexOf(role) != -1) || role == "STEPPER_UP_3") {
                        lastRfidID = scannedID; 
                        activeRfidRole = role; 
                        rfidStep = 0; 
                        brakeMotor(); 
                    }
                }
            }

            if (lastRfidID != "NONE") {
                handleRFIDAction(activeRfidRole, lastRfidID);
            } else {
                processPIDTracking('F'); 
            }
        } 
        else {
            currentAction = "SLEEP_MODE_CHO_LENH_AGV_START";
        }
        
        processTuningCommands();
    }

    stepper_run_non_blocking();
    
    if (millis() - lastDebugTime >= 3000 && !isOTAUpdating) {
        lastDebugTime = millis();
        String report = "\n--- [BÁO CÁO HỆ THỐNG] ---\n";
        report += "HÀNH ĐỘNG XE: " + currentAction + "\n";
        
        char pidBuffer[100];
        sprintf(pidBuffer, "THÔNG SỐ PID: P=%.2f, I=%.2f, D=%.2f | TỰ THÍCH NGHI: %.2f\n", Kp, Ki, Kd, get_auto_adaptive_gain());
        report += String(pidBuffer);
        
        report += "THÔNG SỐ PWM   : BASE=" + String(BASE_PWM) + ", MAX=" + String(DRIVE_MAX_PWM) + "\n";
        
        report += "MẤT LINE TIMEOUT : " + String(lostLineTimeout) + " ms\n";
        report += "ROUTE KẾ HOẠCH   : " + (routePlan == "" ? "[TRỐNG]" : routePlan) + "\n";
        
        uint8_t lineVal = line_sensor_read();
        char lineBuf[30];
        sprintf(lineBuf, "[%d %d %d %d %d]", 
                (lineVal & 0x10) >> 4, 
                (lineVal & 0x08) >> 3, 
                (lineVal & 0x04) >> 2, 
                (lineVal & 0x02) >> 1, 
                (lineVal & 0x01));
        report += "INPUT 5 MẮT LINE : " + String(lineBuf) + " (DEC: " + String(lineVal) + ")\n";
        
        report += "OTA STATE: " + String(OTA_ON ? "BẬT" : "TẮT") + " | SENSOR STATE: " + String(isAgvActive ? "ACTIVE" : "SLEEP") + "\n";
        report += "MOTOR TRÁI  | Trạng thái: " + telemetry_MotL_State + " | Xung: " + String(telemetry_PWM_L) + "\n";
        report += "MOTOR PHẢI  | Trạng thái: " + telemetry_MotR_State + " | Xung: " + String(telemetry_PWM_R) + "\n";
        
        report += "KHOẢNG CÁCH: " + String(currentDistance) + " cm\n";
        report += "RFID: " + String(rfid_check_health() ? "ONLINE - " + lastRfidID : "OFFLINE") + "\n";
        report += "-----------------------------\n";

        Serial.print(report);
        if (tuningClient && tuningClient.connected()) tuningClient.print(report);
    }
}