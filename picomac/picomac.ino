// picomac -- a Macintosh Plus (the umac emulator, Musashi 68000) for the ClockworkPi PicoCalc
// with a Waveshare ESP32-P4-WIFI6 module (and an SDL2 desktop build of the same code).
// Everything lives in src/: hal/ (the machine), frontend/ (menus, game loop) and mac/ (the Mac).
#include "board.h"
#include "src/frontend/app.h"

#if defined(BOARD_PICOCALC_P4)
#include <Arduino.h>
SET_LOOP_TASK_STACK_SIZE(32 * 1024);   // the cores keep line buffers on the stack
#endif

void setup() { app_setup(); }
void loop()  { app_loop(); }
