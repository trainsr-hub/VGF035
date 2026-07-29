// path: stepper_manual_frequency_control.ino
#include <Arduino.h>

#define IN1 15
#define IN2 2
#define IN3 4
#define IN4 16

const int stepMatrixFullStep[4][4] = {
    {1, 1, 0, 0}, 
    {0, 1, 1, 0}, 
    {0, 0, 1, 1}, 
    {1, 0, 0, 1}  
};

bool isClockwise = true;
int stepIndex = 0;
int currentDelayUs = 0; 

/* =========================================================================
   [HIGHLIGHT THAY ĐỔI]: KHAI BÁO BIẾN KIỂM SOÁT SỐ BƯỚC VÀ TRẠNG THÁI
   - Thêm `targetSteps`: Tổng số bước cần đi được tính từ số vòng quay.
   - Thêm `currentStepCount`: Biến đếm số bước đã thực hiện.
   - Thêm `isRunning`: Cờ trạng thái xác định động cơ đang chạy hay dừng.
   - Định nghĩa `STEPS_PER_REV = 2048` để làm chuẩn tính toán.
   ========================================================================= */
long targetSteps = 0;
long currentStepCount = 0;
bool isRunning = false;
const int STEPS_PER_REV = 2048;

void setup() {
    Serial.begin(115200);
    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    pinMode(IN3, OUTPUT);
    pinMode(IN4, OUTPUT);
    
    Serial.println("\n==================================================");
    Serial.println("--- ĐIỀU KHIỂN STEPPER: TẦN SỐ, SỐ VÒNG, CHIỀU ---");
    Serial.println("Nhập theo định dạng: [Tần số] [Số vòng] [Chiều(1=Cùng, 0=Ngược)]");
    Serial.println("Ví dụ: 350, 2.5, 1 (hoặc 350 2.5 1) rồi nhấn Enter");
    Serial.println("==================================================");
}

void doStep(int idx) {
    digitalWrite(IN1, stepMatrixFullStep[idx][0]);
    digitalWrite(IN2, stepMatrixFullStep[idx][1]);
    digitalWrite(IN3, stepMatrixFullStep[idx][2]);
    digitalWrite(IN4, stepMatrixFullStep[idx][3]);
}

void loop() {
    if (Serial.available() > 0) {
        /* =========================================================================
           [HIGHLIGHT THAY ĐỔI]: ĐỌC ĐA GIÁ TRỊ TỪ SERIAL
           - Sử dụng `Serial.parseFloat()` 2 lần cho Tần số và Số vòng.
           - Sử dụng `Serial.parseInt()` cho Chiều quay.
           - Tính toán trực tiếp số bước dựa trên hằng số `STEPS_PER_REV`.
           ========================================================================= */
        float newFreq = Serial.parseFloat();
        float rotations = Serial.parseFloat();
        int dirInput = Serial.parseInt(); 
        
        while (Serial.available() > 0) {
            Serial.read();
        }

        if (newFreq > 0 && rotations > 0) {
            isClockwise = (dirInput == 1); 
            currentDelayUs = (int)(1000000.0 / newFreq);
            
            // Tính toán tổng số bước cần chạy
            targetSteps = (long)(rotations * STEPS_PER_REV);
            currentStepCount = 0; // Reset bộ đếm
            isRunning = true;     // Kích hoạt cờ chạy

            float rpm = (newFreq / (float)STEPS_PER_REV) * 60.0;

            Serial.println("--------------------------------------------------");
            Serial.printf(">> ĐÃ NHẬN LỆNH MỚI:\n");
            Serial.printf("   - Tần số: %.1f Hz (Delay: %d us)\n", newFreq, currentDelayUs);
            Serial.printf("   - Số vòng yêu cầu: %.2f vòng\n", rotations);
            Serial.printf("   - Tổng số bước: %ld bước\n", targetSteps);
            Serial.printf("   - Tốc độ góc lý thuyết: %.2f RPM\n", rpm);
            Serial.printf("   - Chiều quay: %s\n", isClockwise ? "CÙNG CHIỀU" : "NGƯỢC CHIỀU");
            Serial.println("--------------------------------------------------");
        }
    }

    /* =========================================================================
       [HIGHLIGHT THAY ĐỔI]: ĐIỀU KHIỂN SỐ BƯỚC HỮU HẠN
       - Kiểm tra thêm điều kiện `isRunning` và `currentStepCount < targetSteps`.
       - Tăng biến đếm `currentStepCount` sau mỗi bước.
       - Tự động ngắt `isRunning` và in thông báo khi đạt mục tiêu.
       ========================================================================= */
    if (isRunning && currentDelayUs > 0) {
        if (currentStepCount < targetSteps) {
            if (isClockwise) {
                stepIndex = (stepIndex + 1) % 4;
            } else {
                stepIndex = (stepIndex + 3) % 4; 
            }
            
            doStep(stepIndex);
            currentStepCount++;
            delayMicroseconds(currentDelayUs);
        } else {
            // Đã đạt đủ số bước
            isRunning = false;
            Serial.println(">> HOÀN THÀNH LỆNH. CHỜ LỆNH MỚI...");
        }
    }
}

/* =======================================================================================
 * SUMMARY BLOCK: TÓM TẮT THAY ĐỔI CODE
 * 1. Bổ sung các biến kiểm soát hành trình: `targetSteps`, `currentStepCount`, `isRunning`.
 * 2. Thay đổi logic đọc Serial để quét cùng lúc 3 tham số: Tần số (float), Số vòng (float), Chiều quay (int).
 * 3. Thêm công thức quy đổi số vòng quay ra số bước (`rotations * 2048`).
 * 4. Sửa đổi vòng lặp xuất xung: Động cơ chỉ phát xung khi `isRunning == true` và `currentStepCount < targetSteps`. Khi đủ bước, động cơ tự động dừng và chờ lệnh mới.
 * ======================================================================================= */