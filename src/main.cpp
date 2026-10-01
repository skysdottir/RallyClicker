#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <TimeLib.h>
#include "RotaryEncoder.h"

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

/* clicker wheel */
#define CLICK_PIN 28
#define SCROLL_DIR_PIN 30
#define SCROLL_TRIGGER_PIN 29

/* GPS PPS pin - gotta get those micros*/
#define GPS_PPS_PIN 36

/* GPS */
#define GPSBAUD 9600

/* Magic numbers */
#define BUF_SIZE 1024

TinyGPSPlus gps;
Adafruit_ST7789 tft(&SPI1, SOC_GPIO_PIN_T114_TFT_SS, SOC_GPIO_PIN_T114_TFT_DC, SOC_GPIO_PIN_T114_TFT_RST);
RotaryEncoder encoder(SCROLL_TRIGGER_PIN, SCROLL_DIR_PIN, CLICK_PIN);

int lasthr = -1;
int lastmin = -1;
int lastsec = -1;

volatile bool proc_time_sync = false;
volatile bool next_second = false;
long last_gps_second = -1; // records the millisecond we last ticked over a 1s divide, per GPS PPS

bool editing_carnum = false;

int nextcar = -3;
int logged = 0;
int scroll_offset = 0;

int cars[BUF_SIZE];
long times[BUF_SIZE];


void printNum(int num) {
    if (num < 10) {
        tft.print('0');
    }
    tft.print(num);
}

bool time_sync_good = false;

void displayTime() {
    tft.setTextSize(3); // set text size
    tft.setCursor(0, 0); // set cursor position
    
    // font size 1: 6x8, 1 px padding right and bottom
    // font size 2: 12x16, 2 px padding
    // font size 3: 18x24, 3 px padding

    if (time_sync_good) {
        time_t t = now();

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
        tft.printf("XX:XX");
        tft.setTextSize(2);
        tft.setCursor(tft.getCursorX(), 6);
        tft.printf(":XX");
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

    tft.print(" : ");
    printNum(hour(times[buf_idx]));
    tft.print(":");
    printNum(minute(times[buf_idx]));
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
    

    pinMode(GPS_PPS_PIN, INPUT_PULLDOWN);
    attachInterrupt(digitalPinToInterrupt(GPS_PPS_PIN), onGpsPPS, RISING);

    displayTime();
    displayWaitingForGps();
}

long gps_lastbit = 0;
long gps_lastframe = 0;

void loop() {
    bool proc_next_redraw = false;
    bool proc_log_redraw = false;
    bool proc_log_clear = false;

    long t = millis();
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

        if (!time_sync_good || (proc_time_sync && real_secs == 30)) {
            // either we haven't initted our time
            // or this is the first loop of the 30th second of the minute

            int zoned_hour = gps.time.hour() - 7;
            if (zoned_hour < 0) {
                zoned_hour += 24;
            }

            setTime(zoned_hour, 
                    gps.time.minute(), 
                    real_secs, 
                    gps.date.day(), 
                    gps.date.month(), 
                    gps.date.year());

            if (!time_sync_good) {
                proc_log_clear = true;
                proc_next_redraw = true;
            }

            time_sync_good = true;
        }

        proc_time_sync = false;
    }

    encoder.update();

    int delta = encoder.read();

    if (delta > 0 && time_sync_good) {
        if (editing_carnum) {
            nextcar++;
            proc_next_redraw = true;
        } else if (scroll_offset > 0) {
            scroll_offset--;
            proc_log_redraw = true;
        }
    }

    if (delta < 0 && time_sync_good) {
        if (editing_carnum && nextcar > -3) {
            nextcar--;
            proc_next_redraw = true;
        } else if (scroll_offset < logged-5) {
            scroll_offset++;
            proc_log_redraw = true;
        }
    }

    if (encoder.click() && time_sync_good) {
        if (!editing_carnum) {
            long t = now();
            int index = logged % BUF_SIZE;
            cars[index] = nextcar;
            times[index] = t;
            logged++;
            nextcar++;
            scroll_offset = 0;
            proc_next_redraw = true;
            proc_log_redraw = true;
        } else {
            editing_carnum = false;
            proc_next_redraw = true;
        }
    }

    if (encoder.longPress() && time_sync_good) {
        editing_carnum = !editing_carnum;
        proc_next_redraw = true;
    }

    // displayTime does its own deduplication, no need to switch it
    displayTime();

    if (proc_log_clear) {
        clearLog();
    }

    if (proc_next_redraw) {
        displayNextCar();
    }

    if (proc_log_redraw) {
        displayLog();
    }

    if(editing_carnum && millis() % 500 < 20) {
        displayCarnum(-1, nextcar, 32);
    }

    delay(10);
}