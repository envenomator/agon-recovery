#include "zdi.h"
#include "cpu.h"
#include "fabgl.h"
#include "eZ80F92.h"
#include <esp_task_wdt.h>
#include "esp32_io.h"
#include "updater.h"
#include "message.h"
#include "SPIFFS.h"
#include <CRC32.h>

#define PAGESIZE            2048
#define USERLOAD            0x40000
#define BREAKPOINT          0x40020
#define FLASHLOAD           0x50000
#define MOSSIZE_ADDRESS     0x70000
#define MOSDONE             0x70003

#define LEDFLASHFASTMS      75
#define LEDFLASHSTEADYMS    500
#define LEDFLASHWAIT        300

#define WAITHOLDBUTTONMS    2000
#define WAITRESETMS         10000
#define WAITPROGRAMSECS     10
#define ZDI_TCKPIN 26
#define ZDI_TDIPIN 27

fabgl::PS2Controller    PS2Controller;
fabgl::VGA16Controller  DisplayController;
fabgl::Terminal         terminal;

int ledpins[] = {2,4,16}; // should cover most esp32 boards, including ezsbc board
int ledpins_onstate[] = {1,1,0}; // 1: Pin high == on, 0: Pin low == on

uint32_t expected_moscrc;
unsigned long time0;
bool btn_pressed;
bool needmenu;

CPU*                    cpu;
ZDI*                    zdi;

char zdi_msg_up[] = "ZDI up - id 0x%04X, revision 0x%02X\r\n";
char zdi_msg_down[] = "ZDI down - check cabling\r\n";
char menuHeader[] = "Agon recovery utility v1.0\r\n\r\n";
char menuMessage[] = "1: Recover MOS and VDP (Recovery utility running on internal ESP32)\r\n2: Recover MOS only\r\n3: Recover VDP only (Recovery utility running on internal ESP32)\r\n\r\nSelect option >";

void setupLedPins(void) {
    for(int n = 0; n < (sizeof(ledpins) / sizeof(int)); n++)
        directModeOutput(ledpins[n]);
}

void ledsOff(void) {
    for(int n = 0; n < (sizeof(ledpins) / sizeof(int)); n++)
        if(ledpins_onstate[n]) directWriteLow(ledpins[n]);
        else directWriteHigh(ledpins[n]);
}

void ledsOn(void) {
    for(int n = 0; n < (sizeof(ledpins) / sizeof(int)); n++)
        if(ledpins_onstate[n]) directWriteHigh(ledpins[n]);
        else directWriteLow(ledpins[n]);
}

void ledsFlash(void) {
    ledsOff();
    delay(LEDFLASHFASTMS);
    ledsOn();
    delay(LEDFLASHFASTMS);
}

void ledsErrorFlash(void) {
    int n;
    for(n = 0; n < 4; n++) ledsFlash();
    delay(LEDFLASHWAIT);
}

void ledsWaitFlash(int32_t ms) {
    while(ms > 0) {
        ledsOff();
        delay(LEDFLASHSTEADYMS);
        ledsOn();
        delay(LEDFLASHSTEADYMS);
        ms -= 2*LEDFLASHSTEADYMS;
    }
}

void vga_status_screen(void) {
    fg_white();
    terminal.write("\e[2J");     // clear screen
    terminal.write("\e[1;1H");   // move cursor to 1,1
    terminal.write(menuHeader);

    if(zdi->get_productid() != 7) {
        fg_red();
        terminal.printf(zdi_msg_down);
        fg_white();
        terminal.write("\r\nConnect two jumper cables between the GPIO and ZDI ports:\r\n");
        terminal.write(" - ESP GPIO26 to ZDI TCK (pin 4)\r\n");
        terminal.write(" - ESP GPIO27 to ZDI TDI (pin 6)\r\n");
        terminal.write("\r\nWhen using an external ESP32, connect ground between:\r\n");
        terminal.write(" - ESP GND to ZDI GND (pin 5)\r\n");
        terminal.write("\r\ndetailed connection diagrams can be found at\r\n");
        terminal.write("https://github.com/envenomator/agon-recovery\r\n\r\n");
    }
    else {
        terminal.printf(zdi_msg_up, zdi->get_productid(), zdi->get_revision());
    }
}

uint32_t getfileCRC(const char *name) {
    CRC32 crc;
    uint32_t size, retsize;
    uint8_t buffer[PAGESIZE];

    FILE *file = fopen(name, "r");
    
    while(size = fread(buffer, 1, PAGESIZE, file)) {
        for(int n = 0; n < size; n++) crc.update(buffer[n]);
    }

    fclose(file);
    return crc.finalize();
}

void setup() {
    // Disable the watchdog timers
    disableCore0WDT(); delay(200);								
    esp_task_wdt_init(30, false); // in case WDT cannot be removed

    // Serial
    Serial.begin(115200);

    // setup ZDI interface
    zdi = new ZDI(ZDI_TCKPIN, ZDI_TDIPIN);
    cpu = new CPU(zdi);

    // setup LED pins
    setupLedPins();

    // configure boot button as input
    directModeInput(0);

    // Start timer
    time0 = millis();
    btn_pressed = false;

    // Boot-up display
    needmenu = true;

    // setup keyboard/PS2
    PS2Controller.begin(PS2Preset::KeyboardPort0, KbdMode::CreateVirtualKeysQueue);

    // setup VGA display
    DisplayController.begin();
    DisplayController.setResolution(VGA_640x480_60Hz);
    
    // setup terminal
    terminal.begin(&DisplayController);
    terminal.enableCursor(true);

    vga_status_screen();

    // Wait a little for serial comms
    delay(1000);

    // Check files from SPIFFS partition
    File bin_MOS;
    File bin_flash;

    if(!SPIFFS.begin(true)){
        displayMessage("An Error has occurred while mounting SPIFFS");
        while(1) ledsFlash();
    }
    bin_MOS = SPIFFS.open("/MOS.bin", "rb");
    if(!bin_MOS || !bin_MOS.size()){
        displayError("Failed to open '/spiffs/MOS.bin' for reading");
        while(1) ledsFlash();
    }
    bin_flash = SPIFFS.open("/flash.bin", "r");
    if(!bin_flash || !bin_flash.size()){
        displayError("Failed to open '/spiffs/flash.bin' for reading");
        while(1) ledsFlash();
    }
    bin_MOS.close();
    bin_flash.close();

    expected_moscrc = getfileCRC("/spiffs/MOS.bin");
}


void init_ez80(void) {
    cpu->setBreak();
    cpu->setADLmode(true);
    cpu->instruction_di();  
    
    // configure SPI
    cpu->instruction_out (SPI_CTL, 0x04);
    // configure default GPIO
    cpu->instruction_out (PB_DDR, 0xff);
    cpu->instruction_out (PC_DDR, 0xff);
    cpu->instruction_out (PD_DDR, 0xff);
    
    cpu->instruction_out (PB_ALT1, 0x0);
    cpu->instruction_out (PC_ALT1, 0x0);
    cpu->instruction_out (PD_ALT1, 0x0);
    cpu->instruction_out (PB_ALT2, 0x0);
    cpu->instruction_out (PC_ALT2, 0x0);
    cpu->instruction_out (PD_ALT2, 0x0);

    cpu->instruction_out (TMR0_CTL, 0x0);
    cpu->instruction_out (TMR1_CTL, 0x0);
    cpu->instruction_out (TMR2_CTL, 0x0);
    cpu->instruction_out (TMR3_CTL, 0x0);
    cpu->instruction_out (TMR4_CTL, 0x0);
    cpu->instruction_out (TMR5_CTL, 0x0);

    cpu->instruction_out (UART0_IER, 0x0);
    cpu->instruction_out (UART1_IER, 0x0);

    cpu->instruction_out (I2C_CTL, 0x0);
    cpu->instruction_out (FLASH_IRQ, 0x0);

    cpu->instruction_out (SPI_CTL, 0x4);

    cpu->instruction_out (RTC_CTRL, 0x0);
    
    // configure internal flash
    cpu->instruction_out (FLASH_ADDR_U,0x00);
    cpu->instruction_out (FLASH_CTRL,0b00101000);   // flash enabled, 1 wait state
    
    // configure internal RAM chip-select range
    cpu->instruction_out (RAM_ADDR_U,0xb7);         // configure internal RAM chip-select range
    cpu->instruction_out (RAM_CTL,0b10000000);      // enable
    // configure external RAM chip-select range
    cpu->instruction_out (CS0_LBR,0x04);            // lower boundary
    cpu->instruction_out (CS0_UBR,0x0b);            // upper boundary
    cpu->instruction_out (CS0_BMC,0b00000001);      // 1 wait-state, ez80 mode
    cpu->instruction_out (CS0_CTL,0b00001000);      // memory chip select, cs0 enabled

    // configure external RAM chip-select range
    cpu->instruction_out (CS1_CTL,0x00);            // memory chip select, cs1 disabled
    // configure external RAM chip-select range
    cpu->instruction_out (CS2_CTL,0x00);            // memory chip select, cs2 disabled
    // configure external RAM chip-select range
    cpu->instruction_out (CS3_CTL,0x00);            // memory chip select, cs3 disabled

    // set stack pointer
    cpu->sp(0x0BFFFF);
    // set program counter
    cpu->pc(0x000000);
}

// Upload to ZDI memory from a buffer
void ZDI_upload(uint32_t address, uint8_t *buffer, uint32_t size, bool report) {

    while(size > PAGESIZE) {
        zdi->write_memory(address, PAGESIZE, buffer);
        address += PAGESIZE;
        buffer += PAGESIZE;
        size -= PAGESIZE;
        ledsFlash();
        if(report) displayMessage(".");
    }
    zdi->write_memory(address, size, buffer);
    if(report) displayMessage(".");
    ledsFlash();
}

// Upload to ZDI memory from a file
uint32_t ZDI_upload(uint32_t address, const char *name, bool report) {
    uint32_t size, retsize;
    uint8_t buffer[PAGESIZE];

    FILE *file = fopen(name, "r");
    
    retsize = 0;
    while(size = fread(buffer, 1, PAGESIZE, file)) {
        zdi->write_memory(address, size, buffer);
        address += size;
        retsize += size;
        ledsFlash();
        if(report) displayMessage(".");
    }

    fclose(file);
    return retsize;
}

uint32_t getZDImemoryCRC(uint32_t address, uint32_t size) {
    CRC32 crc;
    uint32_t crcbytes = PAGESIZE;
    uint8_t buffer[PAGESIZE];
    crc.reset();

    cpu->setBreak();
    while(size) {
        if(size >= PAGESIZE) {
            zdi->read_memory(address, PAGESIZE, buffer);
            address += PAGESIZE;
            size -= PAGESIZE;
        }
        else {
            zdi->read_memory(address, size, buffer);
            crcbytes = size;
            address += size;
            size = 0;
        }
        for(int n = 0; n < crcbytes; n++) crc.update(buffer[n]);
    }
    return crc.finalize();
}

void flashMOS(void) {
    uint32_t size;

    init_ez80();

    // Upload the MOS payload to ZDI memory first
    displayMessage("\r\nUploading MOS...");
    ledsFlash();
    size = ZDI_upload(FLASHLOAD, "/spiffs/MOS.bin", true);
    displayMessage(" done - ");
    if(expected_moscrc == getZDImemoryCRC(FLASHLOAD, size)) {
        displayMessage("CRC32 OK\r\n");
    }
    else {
        displayError("CRC32 ERROR\r\n");
        return;
    }
    zdi->write_memory_24bit(MOSSIZE_ADDRESS, size);
    ledsFlash();
     
    // Upload the flash tool to ZDI memory next, so it can pick up the payload
    displayMessage("Uploading flash tool...");
    ledsFlash();
    ZDI_upload(USERLOAD, "/spiffs/flash.bin", true);
    ledsFlash();

    // Run the CPU from the flash tool address
    displayMessage("\r\nFlashing MOS...");
    cpu->pc(USERLOAD);
    cpu->setContinue(); // start flashloader, no feedback  

    // This is a CRITICAL wait and cannot be interrupted by ZDI
    for(int n = 0; n < WAITPROGRAMSECS; n++) { 
        ledsFlash();
        delay(1000);
        displayMessage(".");
    }
    displayMessage(" done - ");
    
    // Final check
    cpu->setBreak();
    if(expected_moscrc == getZDImemoryCRC(0, size)) {
        displayMessage("CRC32 OK\r\n");
    }
    else {
        displayError("CRC32 ERROR\r\n");
    }
}

void printSerialMenu(void) {
    Serial.printf("\r\n\r\n---------------------------------\r\n");
    Serial.printf(zdi_msg_up, zdi->get_productid(), zdi->get_revision());
    Serial.printf("---------------------------------\r\n");
    Serial.printf(menuHeader);
    Serial.printf(menuMessage);
}

void printVGAMenu(void) {
    terminal.printf("\r\n");
    terminal.printf(menuMessage);
}

void printMenus(void) {
    vga_status_screen();
    printSerialMenu();
    printVGAMenu();
}

void zdiStatusMessage(void) {
    while((zdi->get_productid() != 7)) {
        if(!needmenu) {
            needmenu = true;
        }
        ledsErrorFlash();
        ledsOff();
        Serial.printf(zdi_msg_down);
        vga_status_screen();
        delay(500);
    }
    if(needmenu) {
        printMenus();
        needmenu = false;
    }
    ledsOn();
}

// Return ASCII key from serial or PS/2 keyboard
// or 0 when nothing is pressed
// Non-blocking
char getKey(void) {
    fabgl::Keyboard *kb = PS2Controller.keyboard();
    fabgl::VirtualKeyItem item;
    char key = 0;

    if(Serial.available()) key = Serial.read();
    if(kb->getNextVirtualKey(&item, 0) && item.down) key = item.ASCII;

    return key;
}

void handle_MOS_option(void) {
    char key;

    displayMessage("\r\nRecover MOS (y/n)?");
    while(!(key = getKey()));
    if(toUpperCase(key) == 'Y') {
        flashMOS();
    }
    else displayMessage("\r\nAborted\r\n");
    delay(3000);
}

void handle_VDP_option(void) {
    char key;

    displayMessage("\r\nRecover VDP (y/n)?");
    while(!(key = getKey()));    
    if(toUpperCase(key) == 'Y') {
        displayMessage("\r\nSwitching to VDP partition...");
        switch_ota();
        displayMessage(" done\r\nPress reset button to activate...");
    }
    else displayMessage("\r\nAborted\r\n");
    delay(5000);
}

void handle_FULL_option(void) {
    char key;

    displayMessage("\r\nRecover MOS and VDP (y/n)?");
    while(!(key = getKey()));    
    if(toUpperCase(key) == 'Y') {
        flashMOS();
        delay(500);
        displayMessage("\r\nSwitching to VDP partition...");
        switch_ota();
        displayMessage(" done\r\nPress reset button to activate...");
    }
    else displayMessage("\r\nAborted\r\n");
    delay(3000);
}

void loop() {
    zdiStatusMessage();

    // Handle keyboard
    if(char key = getKey()) {
        switch(key) {
            case '1':
                handle_FULL_option();
                break;
            case '2':
                handle_MOS_option();
                break;
            case '3':
                handle_VDP_option();
                break;
            default:
                break;
        }
        printMenus();
    }

    // Handle physical button
    if(directRead(0) == 0) btn_pressed = true;
    else {
        btn_pressed = false;
        time0 = millis();
    }
    if(btn_pressed && (millis() - time0) > WAITHOLDBUTTONMS) {
        flashMOS();
        delay(3000);
        printMenus();
    }
}
