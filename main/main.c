/*
 * @file    main.c
 * @author  Jeremy Urena
 * @board   ESP-S3-DevKitC-1 v1.1
 * This is an RTOS-based firmware for authenticating a pet at a pet door using a PN532 RFID Reader
 */

#include <stdio.h>
#include <stdlib.h>
#include <esp_log.h>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "pn532_driver_i2c.h"
#include "pn532.h"
#include "iot_servo.h"

#define SCL_PIN    (8)
#define SDA_PIN    (9)
#define RESET_PIN  (-1) // Could be configured if valid
#define IRQ_PIN    (4)
#define SERVO_PIN  (5)

static const char *TAG_0 = "NTAG_READ";
static const char *TAG_1 = "SERVO_CTRL";

static uint16_t calibration_value_0 = 30;
static uint16_t calibration_value_180 = 195;

// Function prototypes
void vReaderRFIDTask(void *pvParameters);
void vServoTestTask(void *pvParameters);
void vCreateRFIDTask(void);
void vCreateServoTestTask(void);

// Error handling
typedef struct {
    pn532_io_t pn532_io;
    esp_err_t error;
} RFIDParams_t;

// RFID Init Params
static RFIDParams_t rfid_task_params = {
    .pn532_io = { 0 },
    .error = 0
};

void app_main() {
    ESP_LOGI(TAG_0, "APP MAIN");
    vTaskDelay(1000 / portTICK_PERIOD_MS);

    vCreateRFIDTask();
    vCreateServoTestTask();
}

void vServoTestTask(void *pvParameters) {
    ESP_LOGI(TAG_1, "SERVO TEST TASK");
    
    for ( ;; ) {
        for (int i = calibration_value_0; i <= calibration_value_180; ++i) {
            iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, i);
            vTaskDelay(20 / portTICK_PERIOD_MS);
        }
        iot_servo_write_angle(LEDC_LOW_SPEED_MODE, 0, calibration_value_0);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void vReaderRFIDTask(void *pvParameters) {
    RFIDParams_t *config = (RFIDParams_t *)pvParameters;

    for ( ;; ) {
        uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0};  // Buffer to store the returned UID
        uint8_t uid_length;                     // Length of the UID (4 or 7 bytes depending on ISO14443A card type)

        // Wait for an ISO14443A type cards (Mifare, etc.).  When one is found
        // 'uid' will be populated with the UID, and uid_length will indicate
        // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
        config->error = pn532_read_passive_target_id(&config->pn532_io, PN532_BRTY_ISO14443A_106KBPS, uid, &uid_length, 0);

        if (ESP_OK == config->error)
        {
            // Display some basic information about the card
            ESP_LOGI(TAG_0, "\nFOUND ISO14443A CARD");
            ESP_LOGI(TAG_0, "UID LENGTH: %d BYTES", uid_length);
            ESP_LOGI(TAG_0, "UID VALUE:"); 
            ESP_LOG_BUFFER_HEX_LEVEL(TAG_0, uid, uid_length, ESP_LOG_INFO);
        }
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void vCreateServoTestTask(void) {
    ESP_LOGI(TAG_1, "INIT SERVO CONTROL");

    servo_config_t servo_cfg = {
        .max_angle = 180,
        .min_width_us = 1000,
        .max_width_us = 2000,
        .freq = 50,
        .timer_number = LEDC_TIMER_0,
        .channels = {
            .servo_pin = { SERVO_PIN },
            .ch        = { LEDC_CHANNEL_0 }
        },
        .channel_number = 1
    };

    iot_servo_init(LEDC_LOW_SPEED_MODE, &servo_cfg);

    BaseType_t xReturned;
    TaskHandle_t xHandle = NULL;

    xReturned = xTaskCreate(vServoTestTask, "servoTest", 2048, NULL, 5, &xHandle);

    if (xReturned != pdPASS) {
        ESP_LOGI(TAG_0, "FAILED TO CREATE TASK!\n");
    }

}

void vCreateRFIDTask(void) {
    ESP_LOGI(TAG_0, "INIT PN532 IN I2C MODE");
    ESP_ERROR_CHECK(pn532_new_driver_i2c(SDA_PIN, SCL_PIN, RESET_PIN, IRQ_PIN, 0, &rfid_task_params.pn532_io));

    do {
        rfid_task_params.error = pn532_init(&rfid_task_params.pn532_io);
        if ( rfid_task_params.error != ESP_OK ) {
            ESP_LOGW(TAG_0, "FAILED TO INIT PN532");
            pn532_release(&rfid_task_params.pn532_io);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    } while( rfid_task_params.error != ESP_OK );

    ESP_LOGI(TAG_0, "GET FIRMWARE VERSION");
    uint32_t version_data = 0;
    do {
        rfid_task_params.error = pn532_get_firmware_version(&rfid_task_params.pn532_io, &version_data);
        if (rfid_task_params.error != ESP_OK) {
            ESP_LOGI(TAG_0, "DID NOT FIND PN53x BOARD");
            pn532_reset(&rfid_task_params.pn532_io);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    } while (rfid_task_params.error != ESP_OK);

    ESP_LOGI(TAG_0, "FOUND CHIP PN5%x", (unsigned int)(version_data >> 24) & 0xFF);
    ESP_LOGI(TAG_0, "FIRMWARE VERSION %d.%d", (int)(version_data >> 16) & 0xFF, (int)(version_data >> 8) & 0xFF);
    ESP_LOGI(TAG_0, "WAITING FOR ISO14443A CARD...");
    
    BaseType_t xReturned;
    TaskHandle_t xHandle = NULL;

    xReturned = xTaskCreate(vReaderRFIDTask, "rfidReader", configMINIMAL_STACK_SIZE * 3, ( void *) &rfid_task_params, tskIDLE_PRIORITY, &xHandle);
    
    if (xReturned != pdPASS) {
        ESP_LOGI(TAG_0, "FAILED TO CREATE TASK!\n");
    }
}