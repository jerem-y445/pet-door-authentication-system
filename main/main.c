#include <stdio.h>
#include <stdlib.h>
#include <esp_log.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include "pn532_driver_i2c.h"
#include "pn532.h"

#define SCL_PIN    (8)
#define SDA_PIN    (9)
#define RESET_PIN  (-1) // Could be configured if valid
#define IRQ_PIN    (4)

static const char *TAG = "NTAG_READ";

void vReaderRFIDTask(void *pvParameters);
void vCreateRFIDTask(void);

typedef struct {
    pn532_io_t pn532_io;
    esp_err_t err;
} RFIDParams_t;

static RFIDParams_t rfid_task_params = {
    .pn532_io = {0},
    .err = 0
};

void app_main()
{
    // -- PN532 init ---
    // pn532_io_t pn532_io;
    // esp_err_t err;
    // -----------------

    printf("APP MAIN\n");

    vTaskDelay(1000 / portTICK_PERIOD_MS);

    ESP_LOGI(TAG, "Init PN532 in I2C mode");
    ESP_ERROR_CHECK(pn532_new_driver_i2c(SDA_PIN, SCL_PIN, RESET_PIN, IRQ_PIN, 0, &rfid_task_params.pn532_io));

    do {
        rfid_task_params.err = pn532_init(&rfid_task_params.pn532_io);
        if (rfid_task_params.err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to init PN532");
            pn532_release(&rfid_task_params.pn532_io);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    } while(rfid_task_params.err != ESP_OK);

    ESP_LOGI(TAG, "Get firmware version");
    uint32_t version_data = 0;
    do {
        rfid_task_params.err = pn532_get_firmware_version(&rfid_task_params.pn532_io, &version_data);
        if (ESP_OK != rfid_task_params.err) {
            ESP_LOGI(TAG, "Didn't find PN53x board");
            pn532_reset(&rfid_task_params.pn532_io);
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    } while (ESP_OK != rfid_task_params.err);

    // Log firmware infos
    ESP_LOGI(TAG, "Found chip PN5%x", (unsigned int)(version_data >> 24) & 0xFF);
    ESP_LOGI(TAG, "Firmware ver. %d.%d", (int)(version_data >> 16) & 0xFF, (int)(version_data >> 8) & 0xFF);

    ESP_LOGI(TAG, "Waiting for an ISO14443A Card...");

    vCreateRFIDTask();

}

void vReaderRFIDTask(void *pvParameters) {
    RFIDParams_t *config = (RFIDParams_t *)pvParameters;

    for ( ;; ) {
        uint8_t uid[] = {0, 0, 0, 0, 0, 0, 0}; // Buffer to store the returned UID
        uint8_t uid_length;                     // Length of the UID (4 or 7 bytes depending on ISO14443A card type)

        // Wait for an ISO14443A type cards (Mifare, etc.).  When one is found
        // 'uid' will be populated with the UID, and uid_length will indicate
        // if the uid is 4 bytes (Mifare Classic) or 7 bytes (Mifare Ultralight)
        config->err = pn532_read_passive_target_id(&config->pn532_io, PN532_BRTY_ISO14443A_106KBPS, uid, &uid_length, 0);

        if (ESP_OK == config->err)
        {
            // Display some basic information about the card
            ESP_LOGI(TAG, "\nFound an ISO14443A card!");
            ESP_LOGI(TAG, "UID Length: %d bytes", uid_length);
            ESP_LOGI(TAG, "UID Value:"); 
            ESP_LOG_BUFFER_HEX_LEVEL(TAG, uid, uid_length, ESP_LOG_INFO);
        }
        
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void vCreateRFIDTask(void) {
    BaseType_t xReturned;
    TaskHandle_t xHandle = NULL;

    xReturned = xTaskCreate(vReaderRFIDTask, "ReadRFID", configMINIMAL_STACK_SIZE * 3, ( void *) &rfid_task_params, tskIDLE_PRIORITY, &xHandle);
    
    if(xReturned != pdPASS) {
        printf("Failed to create task\n");
    }
}
