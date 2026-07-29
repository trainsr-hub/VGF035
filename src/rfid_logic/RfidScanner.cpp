// path: src/rfid_logic/RfidScanner.cpp
#include "RfidScanner.h"
#include <SPI.h>
#include <MFRC522.h>
#include "../config.h"

// Biến nội bộ, cô lập hoàn toàn khỏi file Main
MFRC522 mfrc522(RC522_SDA, RC522_RST);

void rfid_init() {
    pinMode(RC522_RST, OUTPUT);
    digitalWrite(RC522_RST, LOW); delay(50);
    digitalWrite(RC522_RST, HIGH); delay(50);

    mfrc522.PCD_Init();
    delay(4);
    
    // Ép bật 2 chân antenna transmitter
    byte txControl = mfrc522.PCD_ReadRegister(mfrc522.TxControlReg);
    if ((txControl & 0x03) != 0x03) {
        mfrc522.PCD_WriteRegister(mfrc522.TxControlReg, txControl | 0x03);
    }
    // Tăng gain thu sóng lên tối đa
    mfrc522.PCD_SetAntennaGain(mfrc522.RxGain_max);
}

String rfid_scan() {
    // Nếu không có thẻ hoặc không đọc được serial, trả về chuỗi rỗng
    if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
        return "";
    }

    // Trích xuất ID thẻ và chuyển đổi sang dạng String Hexadecimal
    String uid_str = "";
    for (byte i = 0; i < mfrc522.uid.size; i++) {
        uid_str += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
        uid_str += String(mfrc522.uid.uidByte[i], HEX);
    }
    uid_str.toUpperCase();

    // Khóa thẻ để chống đọc lặp và dừng mã hóa
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    return uid_str;
}

bool rfid_check_health() {
    byte rfidVersion = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
    
    // Nếu version hợp lệ (0x91, 0x92, 0x82, 0x88), module đang hoạt động tốt
    if (rfidVersion == 0x91 || rfidVersion == 0x92 || rfidVersion == 0x82 || rfidVersion == 0x88) {
        return true;
    } 
    
    // Nếu sai lệch, module có thể bị sốc điện hoặc lỏng cáp, tiến hành ép reset
    rfid_init();
    return false;
}