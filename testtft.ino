#include "Wire.h"
#include "TFT_eSPI.h"
#include "SHT31.h"
#include <SPI.h>
#include <ModbusMaster.h>
#include "Free_Fonts.h"

typedef struct {
  float V;
  float F;
} READING;

SHT31 sht;
TFT_eSPI tft;
TFT_eSprite spr = TFT_eSprite(&tft);
uint32_t updateTime = 0;

SoftwareSerial SerialMod(D1,D0);
ModbusMaster node;

#define LCD_BACKLIGHT (72Ul)

void setup() {
  Serial.begin(115200);
  SerialMod.begin(9600);
  
  node.begin(17, SerialMod);
  Wire.begin();
  sht.begin();    //SHT31 I2C Address
  
  Wire.setClock(100000);
  
  tft.begin();
  tft.init();
  tft.setRotation(3);
  spr.createSprite(TFT_HEIGHT,TFT_WIDTH);
  spr.setRotation(3);
  
  tft.fillScreen(TFT_BLACK);
  digitalWrite(LCD_BACKLIGHT, HIGH);
}

void loop() {
  sht.read();
  float t = sht.getTemperature();
  float h = sht.getHumidity();

  READING r;
  uint8_t result = node.readHoldingRegisters(0002,10); 

  if (result == node.ku8MBSuccess){
    r.V = (float)node.getResponseBuffer(0x00)/100;
    r.F = (float)node.getResponseBuffer(0x09)/100;
    tft.setTextColor(TFT_GREEN);
    tft.drawString("Modbus",112,5); 
  } else {
    r.V = 0;
    r.F = 0;
    tft.setTextColor(TFT_RED);
    tft.drawString("Modbus",112,5);   
  }

  tft.setFreeFont(&FreeSans9pt7b);
  spr.setFreeFont(&FreeSans9pt7b);
  tft.setTextSize(1);

  tft.drawFastVLine(160,25,220,TFT_DARKCYAN);
  tft.drawFastHLine(0,135,320,TFT_DARKCYAN);
  tft.drawFastHLine(0,25,320,TFT_DARKCYAN);

  //Kuadran 1 - Temperature
  spr.createSprite(158, 102);
  spr.fillSprite(TFT_BLACK);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("Temp",55,8);
  spr.setTextColor(TFT_GREEN);
  spr.setFreeFont(FSSO24);
  spr.drawString(String(t,1),25,36);
  spr.setTextColor(TFT_YELLOW); 
  spr.setFreeFont(&FreeSans9pt7b);
  spr.drawString("C",113,85);
  spr.pushSprite(0,27); 
  spr.deleteSprite();

  //Kuadran 2 - Voltage
  spr.setFreeFont(&FreeSans9pt7b);
  spr.createSprite(158, 102);
  spr.fillSprite(TFT_BLACK);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("Voltage",50,8);
  spr.setTextColor(TFT_GREEN);
  spr.setFreeFont(FSSO24);
  spr.drawString(String(r.V,1),11,36);
  spr.setTextColor(TFT_YELLOW);
  spr.setFreeFont(&FreeSans9pt7b);
  spr.drawString("VAC",105,85);
  spr.pushSprite(162,27); 
  spr.deleteSprite();

  //Kuadran 3 - Frequency
  spr.createSprite(158, 100);
  spr.fillSprite(TFT_BLACK);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("Freq",62,6);
  spr.setTextColor(TFT_GREEN);
  spr.setFreeFont(FSSO24);
  spr.drawString(String(r.F,1),26,34);
  spr.setTextColor(TFT_YELLOW);
  spr.setFreeFont(&FreeSans9pt7b);
  spr.drawString("Hz",108,82);  
  spr.pushSprite(162,137);
  spr.deleteSprite();

  //Kuadran 4 - Humidity
  spr.createSprite(158, 100);
  spr.fillSprite(TFT_BLACK);
  spr.setTextColor(TFT_WHITE);
  spr.drawString("Humidity",43,6);
  spr.setTextColor(TFT_GREEN);
  spr.setFreeFont(FSSO24);
  spr.drawString(String(h,1),25,34);
  spr.setTextColor(TFT_YELLOW);
  spr.setFreeFont(&FreeSans9pt7b);
  spr.drawString("%RH",65,82);
  spr.pushSprite(0,137); 
  spr.deleteSprite();

  // Sensor status indicator
  if (h == 0) {
    tft.setTextColor(TFT_RED);
    tft.drawString("Sensor",183,5);
  } else {
    tft.setTextColor(TFT_GREEN);
    tft.drawString("Sensor",183,5);
  }

  delay(1000);  // Basic refresh rate
}
