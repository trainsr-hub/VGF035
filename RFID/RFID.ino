// path: D:\My Projects\Varis Gear\IDE\rfid_debug\rfid_debug.ino

/*
===============================================================================
 TÓM TẮT CÁC THAY ĐỔI (BẢN DEBUG PHẦN CỨNG CHUYÊN SÂU):
 1. Bổ sung bài Test MOSI trực tiếp: Ghi đè giá trị 0x55 vào thanh ghi ModWidthReg (0x24) rồi đọc lại ngay lập tức.
 2. Tự động nội suy lỗi: Phân tách rõ ràng giữa việc đứt đường MOSI (Lệnh ghi bị từ chối) và lỗi sụt áp nguồn (MOSI ghi thành công nhưng chip tự khởi động lại làm mất cấu hình Antenna).
 3. Tối ưu log Serial: In ra thẳng kết luận để bạn không cần đoán mò.
===============================================================================
*/

#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>

//=============================================================================
// PIN CONFIG
//=============================================================================

#define RC522_SS     5
#define RC522_SCK    18
#define RC522_MISO   19
#define RC522_MOSI   23
#define RC522_RST    22

MFRC522 mfrc522(RC522_SS, RC522_RST);

unsigned long lastReport = 0;
uint32_t recoveryCount = 0;

//=============================================================================
// UTILITIES
//=============================================================================

void line()
{
    Serial.println(F("------------------------------------------------"));
}

bool isRFIDOnline(byte version)
{
    return
        version == 0x91 ||
        version == 0x92 ||
        version == 0x82 ||
        version == 0x88;
}

const char* versionText(byte version)
{
    switch (version)
    {
        case 0x91: return "MFRC522 v1.0";
        case 0x92: return "MFRC522 v2.0";
        case 0x82: return "Clone";
        case 0x88: return "FM17522 Clone";

        case 0x00:
            return "SPI timeout / Reset LOW / Module dead";

        case 0xFF:
            return "No SPI device / MISO floating";

        default:
            return "Unknown";
    }
}

void printHex(byte b)
{
    if (b < 0x10)
        Serial.print('0');

    Serial.print(b, HEX);
}

//=============================================================================
// RFID INIT
//=============================================================================

void initRFID()
{
    mfrc522.PCD_Init();
    delay(5);

    byte txBefore = mfrc522.PCD_ReadRegister(MFRC522::TxControlReg);

    if ((txBefore & 0x03) != 0x03)
    {
        mfrc522.PCD_WriteRegister(MFRC522::TxControlReg, txBefore | 0x03);
    }
    
    mfrc522.PCD_SetAntennaGain(MFRC522::RxGain_max);
}

//=============================================================================
// REPORT & HARDWARE DIAGNOSTIC
//=============================================================================

void reportRFID()
{
    byte version = mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
    byte tx      = mfrc522.PCD_ReadRegister(MFRC522::TxControlReg);

    // --- BÀI TEST CHẾT CHÓC KIỂM TRA ĐƯỜNG MOSI ---
    byte oldMod = mfrc522.PCD_ReadRegister(MFRC522::ModWidthReg);
    mfrc522.PCD_WriteRegister(MFRC522::ModWidthReg, 0x55);
    byte testMod = mfrc522.PCD_ReadRegister(MFRC522::ModWidthReg);
    mfrc522.PCD_WriteRegister(MFRC522::ModWidthReg, oldMod); // Trả lại trạng thái cũ
    /*
     * [HIGHLIGHT THAY ĐỔI]: Ép ghi giá trị 0x55 (Mã test) vào ModWidthReg và đọc lại ngay lập tức.
     * Thao tác này chứng minh mạch SPI có bị liệt chiều đi (MOSI) hay không.
     */

    line();
    Serial.println(F("RFID HARDWARE DIAGNOSTIC"));
    line();

    Serial.print(F("VersionReg : 0x"));
    printHex(version);
    Serial.println();

    Serial.print(F("Meaning    : "));
    Serial.println(versionText(version));

    Serial.print(F("TxControl  : 0x"));
    printHex(tx);
    Serial.println();

    Serial.print(F("Antenna    : "));
    if ((tx & 0x03) == 0x03)
        Serial.println(F("ON"));
    else
        Serial.println(F("OFF"));

    Serial.print(F("MOSI Test  : "));
    if (testMod == 0x55)
        Serial.println(F("PASS (Write OK)"));
    else
        Serial.println(F("FAILED (Write Ignored)"));

    Serial.print(F("Recovery   : "));
    Serial.println(recoveryCount);

    line();
    // TIẾN HÀNH LUẬN TỘI PHẦN CỨNG
    Serial.println(F(">>> KET LUAN LOI PHAN CUNG <<<"));
    
    if (!isRFIDOnline(version)) {
        Serial.println(F("[!] CHET TOAN TAP: MISO hoac SS bi dut, hoac thieu ap nang."));
    } else if (testMod != 0x55) {
        Serial.println(F("[!] LOI BUS SPI: Duong MOSI (GPIO 23) bi dut hoac moi han lanh tren PCB!"));
        Serial.println(F("    ESP32 doc duoc MISO nhung KHONG THE GHI LENH (Bao gom lenh bat Antenna)."));
    } else if ((tx & 0x03) != 0x03) {
        Serial.println(F("[!] LOI NGUON/RESET: Duong MOSI van song, nhung Antenna van bi OFF."));
        Serial.println(F("    Nguyen nhan: Khi lenh bat song truyen toi, dong dien vọt len lam sụt ap 3.3V,"));
        Serial.println(F("    khien chip RC522 reset nguoc lai trang thai goc (Antenna OFF). Kiem tra lai tich dien hoac track GND/VCC!"));
    } else {
        Serial.println(F("[OK] HE THONG BINH THUONG. THE SAN SANG DUOC DOC."));
    }
    line();

    if (!isRFIDOnline(version))
    {
        Serial.println(F("Trying recovery..."));
        recoveryCount++;
        initRFID();
        delay(10);
        byte after = mfrc522.PCD_ReadRegister(MFRC522::VersionReg);
        Serial.print(F("VersionReg : 0x"));
        printHex(after);
        Serial.println();

        if (isRFIDOnline(after))
            Serial.println(F("Recovery   : SUCCESS"));
        else
            Serial.println(F("Recovery   : FAILED"));
    }

    Serial.println();
}

//=============================================================================
// SETUP
//=============================================================================

void setup()
{
    Serial.begin(115200);

    while (!Serial);

    Serial.println();
    Serial.println(F("===================================="));
    Serial.println(F("RC522 RFID Hardware Diagnostic"));
    Serial.println(F("===================================="));

    pinMode(RC522_RST, OUTPUT);

    digitalWrite(RC522_RST, LOW);
    delay(50);

    digitalWrite(RC522_RST, HIGH);
    delay(50);

    SPI.begin(
        RC522_SCK,
        RC522_MISO,
        RC522_MOSI,
        -1
    );

    initRFID();

    reportRFID();

    Serial.println(F("Ready."));
    Serial.println();
}

//=============================================================================
// LOOP
//=============================================================================

void loop()
{
    if (millis() - lastReport >= 2000) // Gian cach 2s cho de doc
    {
        lastReport = millis();
        reportRFID();
    }

    if (!mfrc522.PICC_IsNewCardPresent())
    {
        delay(5);
        return;
    }

    if (!mfrc522.PICC_ReadCardSerial())
    {
        line();
        Serial.println(F("CARD DETECTED"));
        Serial.println(F("ReadCardSerial() FAILED"));
        Serial.println(F("Possible causes:"));
        Serial.println(F(" - Card moved too quickly"));
        Serial.println(F(" - Weak antenna signal"));
        Serial.println(F(" - Communication timeout"));
        Serial.println(F(" - Card collision"));
        line();
        Serial.println();

        delay(300);
        return;
    }

    line();
    Serial.println(F("CARD READ SUCCESS"));
    line();

    Serial.print(F("UID Length : "));
    Serial.println(mfrc522.uid.size);

    Serial.print(F("UID HEX    : "));

    for (byte i = 0; i < mfrc522.uid.size; i++)
    {
        printHex(mfrc522.uid.uidByte[i]);

        if (i != mfrc522.uid.size - 1)
            Serial.print(' ');
    }

    Serial.println();

    Serial.print(F("SAK        : 0x"));
    printHex(mfrc522.uid.sak);
    Serial.println();

    line();
    Serial.println();

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    delay(500);
}