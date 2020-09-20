#include <Wire.h>
#include <SPI.h>
#include "SdFat.h"
#include "MAX30100_PulseOximeter.h"
#include "SSD1306Spi.h"
#include "images.h"

using namespace sdfat;
SdFat SD;

#define SAMPLING_RATE     MAX30100_SAMPRATE_100HZ
#define PULSE_WIDTH       MAX30100_SPC_PW_1600US_16BITS
#define HIGHRES_MODE      true
#define RES               D4
#define DC                D3
#define CS                D0
#define SS                D8
#define btnPin            A0
#define period            50
#define N_samples         60
#define SAVE              true
#define FAIL              false

SSD1306Spi display(RES, DC, CS);

PulseOximeter pox;
MAX30100 sensor;
File myFile;

uint8_t count = 0;
uint16_t last = 0;
uint32_t prev = 0;
bool btnState = 0;

char *dtostrf(double val, signed char width, unsigned char prec, char *s);
void onBeatDetected(void);
void SDbegin(void);
void SDWrite(void);
void SDRead(String);

/////////////////////////////////////////sensor class////////////////////////////////////
class Sensor
{
  protected:
    uint16_t IBI = 0;
    uint16_t BPM = 0;
    uint16_t currCall = 0;
    uint16_t lastCall = 0;
    uint16_t Callt = 0;
    uint16_t RRint[N_samples];
    uint8_t  wave[64];
    uint8_t  n = 0;
  public:
    Sensor()
    {
      for (int i = 0; i < 64; i++)
        wave[i] = 0;
      for (int i = 0; i < N_samples; i++)
        RRint[i] = 0;
    }
    void poxsetup(void);
    void maxsetup(void);
    void senSetup(void);
    void maxupdate(void);
};

void Sensor::poxsetup(void)
{
  Serial.print("Initializing pulse oximeter..");
  if (!pox.begin()) {
    Serial.println("FAILED");
    for (;;);
  } else {
    Serial.println("SUCCESS");
  }
  pox.setIRLedCurrent(MAX30100_LED_CURR_7_6MA);
  pox.setOnBeatDetectedCallback(onBeatDetected);
}

void Sensor::maxsetup(void)
{
  if (!sensor.begin()) {
    Serial.println("FAILED");
    for (;;);
  } else {
    Serial.println("SUCCESS");
  }
  sensor.setMode(MAX30100_MODE_SPO2_HR);
  sensor.setLedsPulseWidth(PULSE_WIDTH);
  sensor.setSamplingRate(SAMPLING_RATE);
  sensor.setHighresModeEnabled(HIGHRES_MODE);
}

void Sensor::maxupdate(void)
{
  currCall = micros();
  Callt = currCall - lastCall;
  //Serial.println(Callt);
  pox.update();
  sensor.update();
  lastCall = micros();
}

void Sensor::senSetup(void)
{
  maxsetup();
  poxsetup();
  n = 0;
  BPM = 0;
}
///////////////////////////////////////display class////////////////////////////////////
class OLED :  public Sensor
{
  protected:
    float RMSSD;
    float SDNN;
    float avgIBI;
    float avgBPM;
  public:
    void dispsetup(void);
    void readScreen(void);
    void waitScreen(void);
    void resScreen(void);
    void SDsaveScreen(bool);
    friend void SDWrite(void);
};

void OLED::dispsetup(void)
{
  display.init();
  display.flipScreenVertically();
  display.drawXbm(0, 0, 128, 64, myBitmap);
  display.display();
  delay(2000);
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(Title_16);
  display.drawString(0, 16, " HRV Analysis" );
  display.drawString(36, 32, "Device");
  display.display();
  delay(2000);
  display.clear();
}

void OLED::readScreen(void)
{
  char buff[30];
  sprintf(buff, "BPM : %3d         %3d", BPM, n);
  display.clear();
  display.setFont(Heading_12);
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.drawString(0, 0, buff);
  for (int i = 0; i < 63; i++)
    display.drawLine(i * 2, 63 - wave[i], (i + 1) * 2, 63 - wave[i + 1]);
  maxupdate();
  display.display();
}

void OLED::waitScreen(void)
{
  display.clear();
  display.drawXbm(0, 0, 128, 64, pressb_bits);
  display.display();
}

void OLED::resScreen(void)
{
  bool state = 1;
  while (state)
  {
    if (analogRead(btnPin) <= 100)
    {
      state = !state;
      delay(500);
    }
    display.clear();
    display.setFont(Heading_12);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    char buff[22];
    char str[6];
    sprintf(buff, "HRV RESULT");
    display.drawString(20, 0, buff);
    display.setFont(Text_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    dtostrf(avgBPM, 4, 2, str);
    sprintf(buff, ": %s", str);
    display.drawString(0, 20, "Avg BPM");
    display.drawString(70, 20, buff);
    dtostrf(avgIBI, 4, 2, str);
    sprintf(buff, ": %s", str);
    display.drawString(0, 30, "Avg IBI");
    display.drawString(70, 30, buff);
    dtostrf(SDNN, 4, 2, str);
    sprintf(buff, ": %s", str);
    display.drawString(0, 40, "SDNN");
    display.drawString(70, 40, buff);
    dtostrf(RMSSD, 4, 2, str);
    sprintf(buff, ": %s", str);
    display.drawString(0, 50, "RMSSD");
    display.drawString(70, 50, buff);
    display.display();
  }
  btnState = 0;
}

void OLED::SDsaveScreen(bool state)
{
  if (state)
  {
    display.clear();
    display.setFont(Heading_12);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(2, 0, "FILE SAVED!");
    display.drawXbm(24, 14, 80, 50, savef_bits);
    display.display();
    delay(1000);
  }
  else
  {
    display.clear();
    display.setFont(Heading_12);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(2, 0, "SAVE FAILED!");
    display.setFont(Text_10);
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.drawString(0, 20, "Please check SD card");
    display.display();
    delay(1000);
  }
}
//////////////////////////////////////main class////////////////////////////////////////
class Main : public OLED
{
  private:
    uint16_t minr = 65535;
    uint16_t maxr = 0;
    uint16_t red, ir;

  public:
    void drawWave(void);
    void init(void);
    void compute(void);
    friend void onBeatDetected(void);
};

void Main::init(void)
{
  dispsetup();
  poxsetup();
  maxsetup();
}

void Main::drawWave(void)
{
  int amp;
  if ((maxr - minr) >= 500)
  {
    minr = 65535;
    maxr = 0;
  }
  while (sensor.getRawValues(&ir, &red))
  {
    if (red < minr)
      minr = red;
    if (red > maxr)
      maxr = red;
    amp = map(red, minr, maxr, 0, 40);
    for (int i = 0 ; i < 63; i++)
      wave[i] = wave[i + 1];
    wave[63] = constrain(amp, 0, 40);
  }
}

void Main::compute(void)
{
  uint32_t sum = 0;
  uint32_t SDNNsum = 0;
  uint32_t RMSSDsum = 0;
  for (int i = 0; i < N_samples; i++)
    sum += RRint[i];
  avgIBI = (float)sum / N_samples;
  avgBPM = avgIBI / 1000;
  avgBPM = 60 / avgBPM;
  for (int i = 0; i < N_samples; i++)
  {
    SDNNsum += (RRint[i] - avgIBI) * (RRint[i] - avgIBI);
    if (i)
      RMSSDsum += (RRint[i - 1] - RRint[i]) * (RRint[i - 1] - RRint[i]);
  }
  SDNN  = sqrt(SDNNsum / N_samples);
  RMSSD = sqrt(RMSSDsum / (N_samples - 1));
}
/////////////////////////////////////////setup and loop//////////////////////////////////
Main  obj;
void setup()
{
  Serial.begin(9600);
  obj.init();
  pinMode(btnPin, INPUT);
}

void loop()
{
  if (analogRead(btnPin) <= 100)
  {
    btnState = !btnState;
    delay(500);
    obj.senSetup();
    count = 0;
  }

  if (btnState)
  {
    obj.maxupdate();
    if ((millis() - prev) >= period)
    {
      obj.readScreen();
      obj.maxupdate();
      obj.drawWave();
      prev = millis();
    }
  }
  else
    obj.waitScreen();
}

void onBeatDetected()
{
  count++;
  obj.IBI = pox.getHeartRate();
  if ((count > 10) && (obj.IBI < (1.5 * last)))
  {
    Serial.println(obj.IBI);
    last = obj.IBI;
    obj.BPM = 60000 / obj.IBI;
    obj.RRint[obj.n] = obj.IBI;
    obj.n++;
    if (obj.n >= N_samples)
    {
      obj.compute();
      obj.resScreen();
      SDWrite();
      count = 0;
      obj.n = 0;
    }
  }
  else if (count < 10)
    last = obj.IBI;
  else {}
}

void SDbegin()
{
 Serial.print("Initializing SD card...");
  if (!SD.begin(SS)) {
    Serial.println("initialization failed!");
    return;
  }
 Serial.println("initialization done.");
}

void SDWrite()
{
  SDbegin();
  static uint16_t id = 0;
  id++;
  char buff[10];
  sprintf(buff, "USER%d.txt", id);
  myFile = SD.open((String)buff, FILE_WRITE);
  if (myFile) {
    Serial.println("Writing SD card");
    myFile.println("IBI Values:");
    for (int i = 0; i < N_samples ; i++)
      myFile.println(obj.RRint[i]);
    myFile.println("------------HRV RESULTS------------");
    myFile.print("Avg IBI   :");
    myFile.println(obj.avgIBI);
    myFile.print("Avg BPM   :");
    myFile.println(obj.avgBPM);
    myFile.print("Avg SDNN  :");
    myFile.println(obj.SDNN);
    myFile.print("Avg RMSSD :");
    myFile.println(obj.RMSSD);
    obj.SDsaveScreen(SAVE);
    Serial.println("done.");
  }
  else {
    obj.SDsaveScreen(FAIL);
  }
  myFile.close();
  SDRead((String)buff);
  display.init();
  display.flipScreenVertically();
}

void SDRead(String Str)
{
  myFile = SD.open(Str);
  if (myFile) {
    Serial.println(Str);
    while (myFile.available()) {
      Serial.write(myFile.read());
    }
    myFile.close();
  } else {
    Serial.println("error opening test.txt");
  }
}
