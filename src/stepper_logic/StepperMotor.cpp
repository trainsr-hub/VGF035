// path: src/stepper_logic/StepperMotor.cpp
#include "StepperMotor.h"
#include "../config.h"

// Các biến trạng thái nội bộ của động cơ
static int currentStepperPos = 0;
static int targetStepperPos = 0;
static unsigned long lastStepMicros = 0;

/* =========================================================================
   [HIGHLIGHT THAY ĐỔI]: KHAI BÁO CẤU HÌNH TẦN SỐ VÀ MA TRẬN FULL-STEP HIGH TORQUE
   - Thêm biến `currentFreqHz` quản lý tiến trình Ramp tăng tốc.
   - Định nghĩa các tham số tần số: Quay thuận Ramp 100Hz -> 550Hz, Quay nghịch bung 600Hz.
   - Thay thế ma trận Half-Step 8 bước bằng ma trận Full-Step 4 bước (kích kép 2 cuộn dây)
     để tối đa hóa mô-men xoắn ở tần số cao.
========================================================================= */
static float currentFreqHz = 100.0;
const float START_FREQ_CW = 100.0;   // Tần số khởi điểm quay thuận (Hz)
const float TARGET_FREQ_CW = 550.0;  // Tần số đích quay thuận (Hz)
const float RAMP_STEP_HZ = 1.0;      // Tốc độ tăng tần số (1Hz / bước)
const float FREQ_CCW = 600.0;        // Tần số cố định khi quay nghịch (Hz)

const int stepMatrixFullStep[4][4] = {
    {1, 1, 0, 0}, // Bước 1: IN1 + IN2
    {0, 1, 1, 0}, // Bước 2: IN2 + IN3
    {0, 0, 1, 1}, // Bước 3: IN3 + IN4
    {1, 0, 0, 1}  // Bước 4: IN4 + IN1
};
/* ========================================================================= */

void stepper_init() {
    pinMode(STEPPER_IN1, OUTPUT);
    pinMode(STEPPER_IN2, OUTPUT);
    pinMode(STEPPER_IN3, OUTPUT);
    pinMode(STEPPER_IN4, OUTPUT);
}

void stepper_set_target(int targetPos) {
    targetStepperPos = targetPos;
}

bool stepper_is_reached() {
    return (currentStepperPos == targetStepperPos);
}

void stepper_run_non_blocking() {
    if (currentStepperPos == targetStepperPos) {
        // Tắt dòng điện thả lỏng cuộn dây khi đã đến đích
        digitalWrite(STEPPER_IN1, LOW);
        digitalWrite(STEPPER_IN2, LOW);
        digitalWrite(STEPPER_IN3, LOW);
        digitalWrite(STEPPER_IN4, LOW);

        // Reset tần số quay thuận về mức khởi điểm 100Hz cho lần chạy tiếp theo
        currentFreqHz = START_FREQ_CW;
        return;
    }

    /* =========================================================================
       [HIGHLIGHT THAY ĐỔI]: ĐIỀU KHUYỂN TẦN SỐ VÀ TÍNH TOÁN DELAY PHÂN THEO CHIỀU QUAY
       - Đã gỡ bỏ delay cứng 4000us. Thời gian trễ `stepDelayMicros` được tính động 
         từ tần số (Hz).
       - Chiều Thuận (target > current): Tăng tần số từ 100Hz lên 550Hz (+1Hz/step).
       - Chiều Nghịch (target < current): Ép chạy trực tiếp ở tần số 600Hz (~1666us).
    ========================================================================= */
    bool isForward = (targetStepperPos > currentStepperPos);
    
    // Chọn tần số hiện tại dựa theo chiều di chuyển
    float activeFreq = isForward ? currentFreqHz : FREQ_CCW;
    unsigned long stepDelayMicros = (unsigned long)(1000000.0 / activeFreq);

    if (micros() - lastStepMicros >= stepDelayMicros) {
        lastStepMicros = micros();
        
        if (isForward) {
            currentStepperPos++;
            // Tăng tần số từng bước cho tới khi đạt ngưỡng 550Hz
            if (currentFreqHz < TARGET_FREQ_CW) {
                currentFreqHz += RAMP_STEP_HZ;
                if (currentFreqHz > TARGET_FREQ_CW) {
                    currentFreqHz = TARGET_FREQ_CW;
                }
            }
        } else {
            currentStepperPos--;
            // Reset biến Ramp về 100Hz để sẵn sàng nếu lần sau chuyển sang quay thuận
            currentFreqHz = START_FREQ_CW;
        }

        // Đọc ma trận Full-step 4 bước (đảm bảo modulo hoạt động đúng với cả tọa độ âm)
        int stepIndex = (currentStepperPos % 4 + 4) % 4;

        digitalWrite(STEPPER_IN1, stepMatrixFullStep[stepIndex][0]);
        digitalWrite(STEPPER_IN2, stepMatrixFullStep[stepIndex][1]);
        digitalWrite(STEPPER_IN3, stepMatrixFullStep[stepIndex][2]);
        digitalWrite(STEPPER_IN4, stepMatrixFullStep[stepIndex][3]);
    }
    /* =========================================================================
       [COMMENT HIGHLIGHT - CHI TIẾT SỬA ĐỔI]:
       1. Loại bỏ hoàn toàn ma trận Half-Step 8 bước cũ và delay cứng 4000us.
       2. Tích hợp thuật toán Ramp nội suy thời gian thực cho chiều Thuận 
          (100Hz -> 550Hz).
       3. Áp dụng tần số tĩnh 600Hz cho chiều Nghịch.
       4. Đảm bảo cấu trúc Non-blocking dựa trên hàm micros() không làm treo CPU.
    ========================================================================= */
}

/* =======================================================================================
 * SUMMARY BLOCK: TÓM TẮT THAY ĐỔI CODE
 * - Chuyển đổi ma trận điều khiển từ Half-Step (8 bước) sang Full-Step 2 pha (4 bước) để đảm bảo mô-men xoắn không bị sụt giảm ở tần số cao.
 * - Loại bỏ khoảng delay tĩnh 4000us (250Hz), thay thế bằng tính toán delay động `stepDelayMicros` theo thời gian thực từ tần số (Hz).
 * - Quay Thuận (`target > current`): Áp dụng thuật toán Ramp tăng tốc tự động từ 100Hz lên 550Hz (+1Hz mỗi bước).
 * - Quay Nghịch (`target < current`): Chạy trực tiếp ở tần số 600Hz (~1666us/bước).
 * - Reset trạng thái Ramp về 100Hz ngay khi động cơ đạt vị trí đích hoặc bắt đầu đổi chiều quay.
 * ======================================================================================= */