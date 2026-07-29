// path: src/gamepad_logic/GamepadController.cpp
#include "GamepadController.h"
#include <Bluepad32.h>

static ControllerPtr myControllers[BP32_MAX_GAMEPADS];
static GamepadData currentData = {false, false, false, false, false, false, false, false, false, false, false};

// Các hàm callback nội bộ
static void onConnectedController(ControllerPtr ctl) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (!myControllers[i]) {
            myControllers[i] = ctl;
            currentData.isConnected = true;
            currentData.justConnected = true;
            break;
        }
    }
}

static void onDisconnectedController(ControllerPtr ctl) {
    for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
        if (myControllers[i] == ctl) {
            myControllers[i] = nullptr;
            currentData.isConnected = false;
            currentData.justDisconnected = true;
            break;
        }
    }
}

void gamepad_init() {
    BP32.setup(&onConnectedController, &onDisconnectedController);
}

void gamepad_update() {
    BP32.update();
}

GamepadData gamepad_get_data() {
    GamepadData dataToReturn = currentData;
    
    // Xóa cờ "just" để tránh Main kích hoạt âm thanh lặp lại liên tục
    currentData.justConnected = false;
    currentData.justDisconnected = false;

    if (!currentData.isConnected) {
        return dataToReturn;
    }

    // Đọc trạng thái tay cầm đang kết nối
    for (auto ctl : myControllers) {
        if (ctl && ctl->isConnected()) {
            uint16_t buttons = ctl->buttons();
            dataToReturn.up    = ctl->dpad() & 0x01;
            dataToReturn.down  = ctl->dpad() & 0x02;
            dataToReturn.right = ctl->dpad() & 0x04;
            dataToReturn.left  = ctl->dpad() & 0x08;
            
            dataToReturn.btnA  = buttons & 0x0001;
            dataToReturn.btnB  = buttons & 0x0002;
            dataToReturn.btnX  = buttons & 0x0004;
            dataToReturn.btnY  = buttons & 0x0008;
            break;
        }
    }
    return dataToReturn;
}