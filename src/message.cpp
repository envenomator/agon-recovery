#include <fabgl.h>
#include "serial.h"
#include "message.h"

extern fabgl::Terminal terminal;

void displayMessage(const char *msg) {
    Serial.printf(msg);
    terminal.printf(msg);
}

void displayError(const char *msg) {
    fg_red();
    displayMessage(msg);
}

void fg_white(void) {
    terminal.write("\e[44;37m"); // background: blue, foreground: white
}
void fg_red(void) {
    terminal.write("\e[41;37m"); // background: blue, foreground: red
}
