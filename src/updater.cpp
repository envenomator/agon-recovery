#include "updater.h"
#include "serial.h"
#include "message.h"

#define BUFFERSIZE 4096
#define BUFFERSPERDOT	8

/*
void flashVDPFirmware(void) {
	char buffer[BUFFERSIZE];
	uint32_t size;
	FILE *file;
	esp_err_t err;
	esp_ota_handle_t update_handle = 0 ;
	const esp_partition_t *update_partition = NULL;
	const esp_partition_t *configured = esp_ota_get_boot_partition();
	const esp_partition_t *running = esp_ota_get_running_partition();

	file = fopen("/spiffs/firmware.bin", "rb");

	update_partition = esp_ota_get_next_update_partition(NULL);
	err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &update_handle);
	if (err != ESP_OK) {
		displayError("esp_ota_begin failed");
		while(1);
	}

	const size_t buffer_size = BUFFERSIZE;
	int tickcount = 0;
	uint8_t input;

	while(size = fread(buffer, 1, BUFFERSIZE, file)) {
		if(++tickcount > BUFFERSPERDOT) {
			tickcount = 0;
			displayMessage(".");
		}


		err = esp_ota_write( update_handle, (const void *)buffer, size);
		if (err != ESP_OK) {
			displayError("esp_ota_write failed");
			while(1);
		}
	}

	err = esp_ota_set_boot_partition(update_partition);
	if (err != ESP_OK) {
		displayError("esp_ota_set_boot_partition failed!");
		while(1);
	}
	fclose(file);
}
*/
void switch_ota(void) {
	esp_err_t err;
	const esp_partition_t *running = esp_ota_get_running_partition();
	const esp_partition_t *update_partition = esp_ota_get_next_update_partition(running);

	// TODO: check if update_partition is valid
	err = esp_ota_set_boot_partition(update_partition);
	if (err != ESP_OK) {
		displayError("esp_ota_set_boot_partition failed!\n\r");
	}
}