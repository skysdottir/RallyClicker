#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <TimeLib.h>
#include <RotaryEncoder.h>

/* ADC */
#define SOC_GPIO_PIN_T114_ADC_EN 6 // P0.06

/* Ext. sensors */
#define SOC_GPIO_PIN_T114_VEXT_EN 21 // P0.21

/* TFT */
#define TFT_WIDTH  135
#define TFT_HEIGHT 240
#define SOC_GPIO_PIN_T114_TFT_MOSI  9 // P1.09
#define SOC_GPIO_PIN_T114_TFT_MISO  11 // P1.11 NC
#define SOC_GPIO_PIN_T114_TFT_SCK   8 // P1.08
#define SOC_GPIO_PIN_T114_TFT_SS    11 // P0.11
#define SOC_GPIO_PIN_T114_TFT_DC    12 // P0.12
#define SOC_GPIO_PIN_T114_TFT_RST   2 // P0.02
#define SOC_GPIO_PIN_T114_TFT_EN    3 // P0.03
#define SOC_GPIO_PIN_T114_TFT_BLGT  15 // P0.15

/* inputs */
#define CLICK_PIN 28
#define SCROLL_DIR_PIN 30
#define SCROLL_TRIGGER_PIN 29
#define MENU_BUTTON_PIN 42

/* GPS PPS pin - gotta get those micros*/
#define GPS_PPS_PIN 36

/* GPS */
#define GPSBAUD 9600

/* File system */
#define CONFIG_FILE_NAME "/rallyclick/config.txt"
#define LOG_FILE_NAME "/rallyclick/log.txt"

/* Magic numbers */
#define BUF_SIZE 1024 // An event is never going to have more than 1024 cars, right?

// don't do this
#define TIME_ZONE_COUNT 38
int timezones[] = {-1200, -1100, -1000, -930, -900, -800, -700, -600, -500, -400, -330, -300, -200, -100, 0, 100, 200, 300, 330, 400, 430, 500, 530, 545, 600, 630, 700, 800, 845, 900, 930, 1000, 1030, 1100, 1200, 1245, 1300, 1400};
int timezone = -700;
int time_zone_index = 6;

TinyGPSPlus gps;
Adafruit_ST7789 tft(&SPI1, SOC_GPIO_PIN_T114_TFT_SS, SOC_GPIO_PIN_T114_TFT_DC, SOC_GPIO_PIN_T114_TFT_RST);
RotaryEncoder encoder(SCROLL_TRIGGER_PIN, SCROLL_DIR_PIN, CLICK_PIN);

// GPS ISR flags
volatile bool proc_time_sync = false;  // T when gps pps pulse arrives, F on next loop()
volatile bool next_second = false;     // T when gps pps pulse arrives, F when gps serial second advances

// main state switches
bool time_sync_good = false;
bool editing_carnum = false;
bool in_menu = false;
bool editing_menu_item = false;
bool just_toggled_menu_state = true;

int nextcar = -3; // -3: ADV  -2: 000  -1: 00
int logged = 0;
int scroll_offset = 0;

// Menu button debounce
long last_menu_off = 0;
int menu_index = 0;
#define MENU_ITEM_COUNT 1

// the in-memory log store
int cars[BUF_SIZE];
long times[BUF_SIZE];


void printNum(int num) {
    if (num < 10) {
        tft.print('0');
    }
    tft.print(num);
}

int lasthr = -1;
int lastmin = -1;
int lastsec = -1;

// fortunately we only care about hours and minutes. Days can be wrong, they aren't displayed
time_t toTimeZone(time_t t) {
    unsigned long magic = (unsigned long) t;
    magic += (timezone % 100) * 60;
    magic += (timezone / 100) * 3600;
    return (time_t) magic;
}

void displayTime() {
    tft.setTextSize(3); // set text size
    tft.setCursor(0, 0); // set cursor position
    
    // font size 1: 6x8, 1 px padding right and bottom
    // font size 2: 12x16, 2 px padding
    // font size 3: 18x24, 3 px padding

    if (time_sync_good) {

        // now() should return UTC time
        time_t t = toTimeZone(now());

        int t_hour = hour(t);
        int t_minute = minute(t);
        int t_second = second(t);

        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);

        if (lasthr != t_hour) {
            printNum(t_hour);
            lasthr = t_hour;
        }

        if (lastmin != t_minute) {
            tft.setCursor(33, 0);
            tft.print(":");
            printNum(t_minute);
            lastmin = t_minute;
        }

        if (lastsec != t_second) {
            tft.setTextSize(2);
            tft.setCursor(88, 6);
            tft.print(":");
            printNum(t_second);
            lastsec = t_second;
        }
        
    } else {
        tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
        tft.print("XX:XX");
        tft.setTextSize(2);
        tft.setCursor(tft.getCursorX(), 6);
        tft.print(":XX");
    }
}

void displayCarnum(int idx, int num, int y) {
    tft.setTextSize(2);

    if (idx == -1) {
        if (editing_carnum && millis() % 1000 < 500) {
            tft.setTextColor(0x4208, ST77XX_BLACK);
        } else {
            tft.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
        }
    } else if (num < 1) {
        tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    } else if (idx == 0 && scroll_offset == 0) {
        tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    } else {
        tft.setTextColor(0xBDD7, ST77XX_BLACK);
    }
    tft.setCursor(0, y);

    switch(num) {
        case -3:
        tft.print("ADV");
        break;
        case -2:
        tft.print("000");
        break;
        case -1:
        tft.print(" 00");
        break;
        default:
        tft.printf("%3d", num);
    }
}

void displayEntry(int draw_idx) {
    int y = 52 + draw_idx*20;
    
    int buf_idx = (logged - draw_idx - scroll_offset - 1);
    
    if (buf_idx < 0) {
        // blank this row
        tft.setTextColor(ST77XX_BLACK, ST77XX_BLACK);
        tft.setCursor(0, y);
        for (int i = 0; i < 11; i++) {
            tft.print((char) 0xDA);
        }
        return;
    }

    buf_idx %= BUF_SIZE;

    int seqnum = cars[buf_idx];

    displayCarnum(draw_idx, seqnum, y);

    // Stored in UTC, display in current time zone
    time_t zoned_t = toTimeZone(times[buf_idx]);
    tft.print(" : ");
    printNum(hour(zoned_t));
    tft.print(":");
    printNum(minute(zoned_t));
}

void displayWaitingForGps() {
    tft.drawLine(0, 28, 135, 28, ST77XX_WHITE);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setCursor(28, 52);
    tft.print("waiting");
    tft.setCursor(44, 72);
    tft.print("for");
    tft.setCursor(44, 92);
    tft.print("GPS");
    tft.setCursor(40, 112);
    tft.print("time");
    tft.setCursor(40, 132);
    tft.print("sync");
}

void clearLog() {
    tft.drawLine(0, 28, 135, 28, ST77XX_WHITE);
    tft.fillRect(0, 29, 135, 240, ST77XX_BLACK);
}

void displayNextCar() {
    tft.drawLine(0, 28, 135, 28, ST77XX_WHITE);
    displayCarnum(-1, nextcar, 32);
}

void displayLog() {
    tft.setTextSize(2);

    // draw the log. 10 here is the number of lines to display.
    for(int i = 0; (i < 10); i++) {
        displayEntry(i);
    }
}

void displayMenuItem(int index) {
    int first_row_y = 52 + index*40;
    int second_row_y = 52 + index*40 + 20;

    int first_row_fg_color = 0xBDD7;
    int second_row_fg_color = 0xBDD7;
    int bg_color = ST77XX_BLACK;

    // this is the active item
    if (index == menu_index) {
        if (editing_menu_item) {
            first_row_fg_color = ST77XX_CYAN;
            if (millis() % 1000 < 500) {
                second_row_fg_color = ST77XX_CYAN;
            } else {
                second_row_fg_color = 0xBDD7;
            }
        } else {
            first_row_fg_color = ST77XX_WHITE;
            second_row_fg_color = ST77XX_WHITE;
        }
        
        bg_color = 0x4208;
    }

    tft.setTextSize(2);
    tft.setTextColor(first_row_fg_color, bg_color);
    tft.setCursor(0, first_row_y);
    switch(index) {
        case 0:
            tft.print("time zone");
            break;
        case 1:
            tft.print("exit menu");
            break;
        default: break;
    }

    tft.setTextColor(second_row_fg_color, bg_color);
    tft.setCursor(0, second_row_y);

    int pretty_minutes = timezone%100;
    if (pretty_minutes < 0) {
            pretty_minutes *= -1;
    }

    switch(index) {
        case 0: 
            tft.print("UTC");
            tft.printf("%+03d:%02d", timezone/100, pretty_minutes);
            break;
        default: break;
    }
}

void displayMenu() {
    tft.drawLine(0, 28, 135, 28, ST77XX_WHITE);
    for (int i = 0; i <= MENU_ITEM_COUNT; i++) {
        displayMenuItem(i);
    }
}

void incrementMenuItem(int dir) {
    switch(menu_index) {
        case 0: // time zone
            if (dir < 0 && time_zone_index > 0) {
                time_zone_index--;
                timezone = timezones[time_zone_index];
            } else if (dir > 0 && time_zone_index < TIME_ZONE_COUNT - 1) {
                time_zone_index++;
                timezone = timezones[time_zone_index];
            }
        case 1: break; // can't scroll the exit button
        default: break;
    }
}

void onGpsPPS() {
    proc_time_sync = true;
    next_second = true;
}

void setup(void) {

    // enable vext (not required for screen to work, but probably for antenna boost?)
    pinMode(SOC_GPIO_PIN_T114_VEXT_EN, OUTPUT);
    digitalWrite(SOC_GPIO_PIN_T114_VEXT_EN, HIGH);

    delay(100); // Give power time to stabilize

    // enable power to display
    digitalWrite(SOC_GPIO_PIN_T114_TFT_EN, LOW);
    pinMode(SOC_GPIO_PIN_T114_TFT_EN, OUTPUT);

    // enable backlight led (display is off without this)
    digitalWrite(SOC_GPIO_PIN_T114_TFT_BLGT, LOW);
    pinMode(SOC_GPIO_PIN_T114_TFT_BLGT, OUTPUT);

    // enable adc (not required for screen to work, but probably for measuring battery level)
    digitalWrite(SOC_GPIO_PIN_T114_ADC_EN, HIGH);
    pinMode(SOC_GPIO_PIN_T114_ADC_EN, OUTPUT);

    Serial.begin(115200);
    Serial.println("init");
    Serial2.begin(GPSBAUD);

    // init tft
    tft.init(TFT_WIDTH, TFT_HEIGHT);
    tft.setRotation(2);
    tft.setSPISpeed(40000000);

    // show something on the display
    tft.fillScreen(ST77XX_BLACK); // clear the screen
    tft.setTextColor(ST77XX_WHITE); // set text color to white
    tft.setTextSize(2); // set text size
    tft.setCursor(0, 0); // set cursor position
    tft.print("Booting...");

    encoder.setRepeatTiming(200, 60);
    encoder.useInternalPullups(false);
    encoder.enableInterrupts();
    encoder.enableLongPress(1500);
    encoder.begin(false);
    
    InternalFS.begin();

    pinMode(GPS_PPS_PIN, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(GPS_PPS_PIN), onGpsPPS, RISING);

    pinMode(MENU_BUTTON_PIN, INPUT);
    last_menu_off = millis();

    displayTime();
    displayWaitingForGps();
}

long gps_lastbit = 0;
long gps_lastframe = 0;
long last_gps_second = -1; // tracks the second we're seeing in serial messages from GPS

void loop() {
    bool proc_time_redraw = false;
    bool proc_next_car_redraw = false;
    bool proc_log_redraw = false;
    bool proc_log_clear = false;
    bool proc_menu_redraw = false;
    bool proc_menu_item_redraw = false;

    long t = millis();

    // update the last menu button time immediately
    if (digitalRead(MENU_BUTTON_PIN)) {
        last_menu_off = t;
        just_toggled_menu_state = false;
    }

    while(Serial2.available() > 0) {
        int buf = Serial2.read();
        if(gps.encode(buf)) {
            gps_lastframe = t;
        }
        gps_lastbit = t;
    }

    if (gps.time.isValid() && (gps.date.year() > 2020)) {
        // we've got datetime!
        
        int serial_secs = gps.time.second();

        if (serial_secs != last_gps_second) {
            // if the serial seconds have advanced, great, we no longer need to add 1.
            next_second = false;
            last_gps_second = serial_secs;
        }

        int real_secs = (next_second) ? serial_secs + 1 : serial_secs;

        proc_time_redraw = (real_secs != lastsec);

        if (!time_sync_good || (proc_time_sync && real_secs == 30)) {
            // either we haven't initted our time
            // or this is the first loop of the 30th second of the minute

            // set time in UTC, as it should be
            setTime(gps.time.hour(), 
                    gps.time.minute(), 
                    real_secs, 
                    gps.date.day(), 
                    gps.date.month(), 
                    gps.date.year());

            proc_time_redraw = true;
            if (!time_sync_good && !in_menu) {
                proc_log_clear = true;
                proc_next_car_redraw = true;
            }

            time_sync_good = true;
        }

        proc_time_sync = false;
    }

    // now the IO stuff

    // menu button - long press to enter, short press to exit
    if (!in_menu && !just_toggled_menu_state && (t - last_menu_off > 1500)) {
        in_menu = true;
        editing_menu_item = false;
        editing_carnum = false;
        menu_index = 0;
        proc_log_clear = true;
        proc_menu_redraw = true;
        just_toggled_menu_state = true;
    } else if (in_menu && !just_toggled_menu_state && (t - last_menu_off > 50)) {
        in_menu = false;
        editing_menu_item = false;
        proc_log_clear = true;
        just_toggled_menu_state = true;
        proc_next_car_redraw = true;
        proc_log_redraw = true;
    }

    encoder.update();
    int delta = encoder.read();

    if (delta > 0) {
        if (editing_carnum) {
            nextcar++;
            proc_next_car_redraw = true;
        }else if (in_menu) {
            if (editing_menu_item) {
                incrementMenuItem(1);
                proc_menu_item_redraw = true;
            } else if (menu_index < MENU_ITEM_COUNT) {
                menu_index++;
                proc_menu_redraw = true;
            }
        } else if (scroll_offset > 0) {
            scroll_offset--;
            proc_log_redraw = true;
        }
    }

    if (delta < 0) {
        if (editing_carnum && nextcar > -3) {
            nextcar--;
            proc_next_car_redraw = true;
        } else if (in_menu) {
            if (editing_menu_item) {
                incrementMenuItem(-1);
                proc_menu_item_redraw = true;
            } else if (menu_index > 0) {
                menu_index--;
                proc_menu_redraw = true;
            }
        }else if (scroll_offset < logged-5) {
        scroll_offset++;
        proc_log_redraw = true;
        }
    }

    if (encoder.click() ) {
        if (in_menu) {
            if (menu_index == MENU_ITEM_COUNT) {
                // quit is always the last option
                in_menu = false;
                proc_log_clear = true;
                proc_next_car_redraw = true;
                proc_log_redraw = true;
            } else {
                editing_menu_item = !editing_menu_item;
                proc_menu_redraw = true;
            }
        } else if (editing_carnum) {
            editing_carnum = false;
            proc_next_car_redraw = true;
        } else if (time_sync_good) {
            long t = now();
            int index = logged % BUF_SIZE;
            cars[index] = nextcar;
            times[index] = t;
            logged++;
            nextcar++;
            scroll_offset = 0;
            proc_next_car_redraw = true;
            proc_log_redraw = true;
        }
    }

    if (encoder.longPress() && !in_menu && time_sync_good) {
        editing_carnum = !editing_carnum;
        proc_next_car_redraw = true;
    }

    if(proc_time_redraw) {
        displayTime();
    }

    // blinky
    if(millis() % 500 < 20) {
        if(!in_menu && editing_carnum) {
            proc_next_car_redraw = true;
        } else if (in_menu && editing_menu_item) {
            proc_menu_item_redraw = true;
        }
    }

    if (proc_log_clear) {
        clearLog();
    }

    if (time_sync_good) {
        if (proc_next_car_redraw) {
            displayNextCar();
        }

        if (proc_log_redraw) {
            displayLog();
        } 
    } else if (proc_next_car_redraw || proc_log_redraw) {
        displayWaitingForGps();
    }

    if (proc_menu_redraw) {
        displayMenu();
    }

    if (proc_menu_item_redraw) {
        displayMenuItem(menu_index);
    }

    delay(10);
}