// path: src/gamepad_logic/GamepadController.h
#ifndef GAMEPAD_CONTROLLER_H
#define GAMEPAD_CONTROLLER_H

#include <Arduino.h>

struct GamepadData {
    bool isConnected;
    bool justConnected;    // Báo hiệu sườn LÊN để Main phát nhạc kết nối
    bool justDisconnected; // Báo hiệu sườn XUỐNG để Main phát nhạc ngắt kết nối
    bool up;
    bool down;
    bool left;
    bool right;
    bool btnA;
    bool btnB;
    bool btnX;
    bool btnY;
};

void gamepad_init();
void gamepad_update();
GamepadData gamepad_get_data();

#endif