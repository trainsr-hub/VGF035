// path: src/audio_logic/Jq8900Player.cpp
#include "Jq8900Player.h"
#include "../config.h"

void audio_init() {
    // Khởi tạo Serial2 chuyên dụng cho JQ8900. RX = -1 vì ta chỉ truyền lệnh, không nhận phản hồi.
    Serial2.begin(9600, SERIAL_8N1, -1, JQ8900_TX);
}

void audio_play_track(uint16_t trackNumber) {
    uint8_t cmd[6] = {0xAA, 0x07, 0x02, (uint8_t)(trackNumber >> 8), (uint8_t)(trackNumber & 0xFF), 0};
    // Tính Checksum
    cmd[5] = (cmd[0] + cmd[1] + cmd[2] + cmd[3] + cmd[4]) & 0xFF; 
    Serial2.write(cmd, 6);
}

void audio_stop() {
    // Lệnh Hex chuẩn dừng phát nhạc của họ chip JQ8900
    uint8_t cmd[4] = {0xAA, 0x04, 0x00, 0xAE};
    Serial2.write(cmd, 4);
}