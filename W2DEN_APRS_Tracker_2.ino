/*
* This is W2DEN's attempt at a Teensy3.1 APRS Tracker.
* This is V 2 adding a menu system to allow user input.
*
* Compiler Macro Substitutes (#define) in ALL_CAPS with underline for spaces
*
* The bases for this sketch comes from Richard Nash (KC3ARY). Without his code I'd still be searching
* Also thanks to:
*
    Ben Buxton: rotary encoder code ( http://www.buxtronix.net/2011/10/rotary-encoders-done-properly.html )
    9W2SVT: ASPRS Arduino with Display ( http://9w2svt.blogspot.com/ )
    M1GEO: APRS Blog ( http://www.george-smart.co.uk/wiki/APRS )
    KI4MCW: Balloonery ( https://sites.google.com/site/ki4mcw/ )
    BeRTOS Project ...
    Jinseok Jeon (JeonLab.wordpress.com): UTC calculator
    The APRS libraries come from the Trackduino project with modes by many before it got here.
*
********************************************************************
*
*/
#define thisver "2.00" ////////////////////////////////// VERSION
// these define the starting EEPROM addresses.
// easier to change these than dig for the constants.
#define UTC_OFFSET 1
#define XMIT_TIME  2
#define MY_CALL    4
#define MY_SSID   10
#define DEST_CALL 11
#define DEST_SSID 17
#define SYM_TABLE 18
#define SYMBOL    19
#define COMMENT   20
#define SB_ENABLE    55 // these are SmartBeaconing eePROM addresses
#define SBFAST_SPEED 56
#define SBFAST_RATE  58
#define SBSLOW_SPEED 60
#define SBSLOW_RATE  62
#define SBTURN_TIME  64
#define SBTURN_ANGLE 66
#define SBTURN_SLOPE 68
#define AXDELAY      70
#define AXFLAGS      72
#define AXVOXON      74
#define AXVOXSILENT  76
#define PTT_PIN      78

#define NUM_SYMBOLS 10 // number of symbols in the symbols[] table

// includes
#include <WProgram.h>
#include <GPS.h>
#include <aprs.h>
#include <EEPROMex.h> // expanded EEPROM library

#include "SPI.h" // Set up the display
#include "ILI9341_t3.h"
ILI9341_t3 tft = ILI9341_t3(10, 9, 8, 11, 14, 12);
static const int line = 25; //# of lines on screen @ font size 3

// rotary selector
#include <Bounce.h>
#define ROTARY_PIN1 5
#define ROTARY_PIN2 6
#define BUTTONPIN   4
#define DIR_CCW 0x10
#define DIR_CW 0x20
Bounce pushbutton = Bounce(BUTTONPIN, 10);
// Use the full-step state table (emits a code at 00 only)
const unsigned char ttable[7][4] = {
  {0x0, 0x2, 0x4,  0x0}, {0x3, 0x0, 0x1, 0x10},
  {0x3, 0x2, 0x0,  0x0}, {0x3, 0x2, 0x1,  0x0},
  {0x6, 0x0, 0x4,  0x0}, {0x6, 0x5, 0x0, 0x20},
  {0x6, 0x5, 0x4,  0x0},
};
volatile unsigned char state = 0;
void rotary_init() { // setas up the rotatry pins
  pinMode(ROTARY_PIN1, INPUT_PULLUP);
  pinMode(ROTARY_PIN2, INPUT_PULLUP);
  pinMode(BUTTONPIN, INPUT_PULLUP);
}
/* Read input pins and process for events. Call this either from a
 * loop or an interrupt (eg pin change or timer).
 * Returns 0 on no event, otherwise 0x80 or 0x40 depending on the direction.
 */
unsigned char rotary_process() {
  unsigned char pinstate = (digitalRead(ROTARY_PIN2) << 1) | digitalRead(ROTARY_PIN1);
  state = ttable[state & 0xf][pinstate];
  return (state & 0x30);
}
int DaysAMonth[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};//number of days a month
int gpsYear;
int gpsMonth;
int gpsDay = 0;
int gpsHour;
#define knotToMPH 1.15078 // GPS speed is in knots... this is the conversion
////////////////////////////////////////////////////////////////////////////////

// Define the I/O pins
//#define PTT_PIN 13 // Push to talk pin tis is now eePROM
#define ROTARY_PIN1 5
#define ROTARY_PIN2 6
#define BUTTONPIN   4

////////////// user data set up //////////////////////////////
//
//this is all new. User data are read from EEPROM
//
// create the struct instance for the calls and paths//////////
//
struct PathAddress addresses[] = {
  {(char *)NULL, 0},  // Destination callsign
  {(char *)NULL, 0},  // Source callsign
  {(char *)NULL, 0},  // Digi1 (first digi in the chain)
  {(char *)NULL, 0}   // Digi2 (second digi in the chain)
};
//
// set up variables to hold EEPROM data //////////////////////
// make these global so we can edit them.
// variables are loaded to each xmit
//
int8_t TimeZone;    // utc offseti
uint16_t sTime;      // xmit delay if no smart beacon
char sCall[7];      // holds the s source call
char dCall[7];      // holds the d destination call
char buf;           // buffer for EEPROM read
char symTable;      // symbole table \ or //
char symbol;        // symbol > car etc/
char myComment[36]; // comments holder
const char* symbols [][9] = { //table of symbols
  {"House", "/", "/"},
  {"Car", "/", ">"},
  {"M-Cycle", "/", "<"},
  {"Van", "/", "v"},
  {"Truck", "/", "k"},
  {"Bike", "/", "b"},
  {"Balloon", "/", "O"},
  {"SailBoat", "/", "y"},
  {"PwrBoat", "/", "s"},
  {"School", "/", "K"}
};
int8_t   sbEnable;       // use sb or not
uint16_t sbFastSpeed;    //mph speeds stored and compared as integers tostop oscilatting
uint16_t sbFastRate;     //seconds
uint16_t sbSlowSpeed;    // mph as integer
uint16_t sbSlowRate;
uint16_t sbMinTurnTime;  //sec
uint16_t sbMinTurnAngle; //degrees
uint16_t sbTurnSlope;    //
uint16_t axDelay;        // milliseconds
uint16_t axFlags;        // number of flags to send
uint16_t axVoxOn;        // mseconds vox tone sent to xmitter 0 for off
uint16_t axVoxSilent;    // mseconds VOX tone silent  0 for off
uint16_t pttPin;         // PTT Teensy Pin usually 13, 0 for off

uint16_t mySpeed   = 0;  // Holds gps.speed for processing
uint16_t myHeading = 0;
uint16_t sbSpeed   = 0;  //prior read speed
uint16_t sbHeading = 0;  // prior heading
//////////////////////// pre set up variables///////////////////////////
HardwareSerial &gpsSerial = Serial1;
GPS gps(&gpsSerial, true);
void mNumChoice(int eePromAddress, int8_t *variable, int lTZ, int uTZ,  int nTZ, String title, String help , uint16_t TTT = 0, uint16_t *variable2 = 0);

uint32_t dTime;
uint32_t timeOfAPRS = 0;
bool gotGPS = false;

////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////// setup()
////////////////////////////////////////////////////////////////////////
void setup()
{
  Serial.begin(9600); // For debugging output over the USB port
  // read the EEPROM user data into memory /// will be updated/////////
  /////////////////////////////////////////////////////////////////////
  TimeZone = EEPROM.read(UTC_OFFSET);
  // sTime is the start delay time sotred in eePROM as an int in seconds
  // dTime is the working xmit delay uint32_t starts as sTime then via SmartBeacon if activated.
  sTime = EEPROM.readInt(XMIT_TIME); // seconds
  dTime = sTime * 1000; // store the start time into dTime (delay) in milliseconds
  for (int i = MY_CALL; i < MY_CALL + 7; i++) { // Source call
    buf = EEPROM.read(i);
    if (buf == 32) break;
    sCall[i - MY_CALL] = buf;
  }
  for (int i = DEST_CALL; i < DEST_CALL + 7; i++) { //destinationn call
    buf = EEPROM.read(i);
    if (buf == 32) break;
    dCall[i - DEST_CALL] = buf;
  }

  for (int i = COMMENT; i < COMMENT + 35; i++) { // comment
    buf = EEPROM.read(i);
    myComment[i - COMMENT] = buf;
  }
  // now the SmartBeacon parameters...
  // 16 bit allows menu compatability
  sbEnable       = EEPROM.read(SB_ENABLE);
  sbFastSpeed    = EEPROM.readInt(SBFAST_SPEED);  //mph speeds stored and compared as integers tostop oscilatting
  sbFastRate     = EEPROM.readInt(SBFAST_RATE);    //seconds
  sbSlowSpeed    = EEPROM.readInt(SBSLOW_SPEED);     // mph as integer
  sbSlowRate     = EEPROM.readInt(SBSLOW_RATE);
  sbMinTurnTime  = EEPROM.readInt(SBTURN_TIME);  //sec
  sbMinTurnAngle = EEPROM.readInt(SBTURN_ANGLE); //degrees
  sbTurnSlope    = EEPROM.readInt(SBTURN_SLOPE);   //
  axDelay        = EEPROM.readInt(AXDELAY);    // milliseconds
  axFlags        = EEPROM.readInt(AXFLAGS);    // number of flags to send
  axVoxOn        = EEPROM.readInt(AXVOXON );    // mseconds vox tone sent to xmitter 0 for off
  axVoxSilent    = EEPROM.readInt(AXVOXSILENT);   // mseconds VOX tone silent  0 for off
  pttPin         = EEPROM.readInt(PTT_PIN);   // PTT Teensy Pin usually 13, 0 for off

  tft.begin();
  tft.fillScreen(ILI9341_BLACK);
  tft.setRotation(0);
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft.setCursor(0, 50);
  tft.println("W2DEN's");
  tft.println("APRS");
  tft.println("Tracker");
  tft.println("Loading ...");

  // start the GPS polling and wait for valid data... may take 15+ seconds
  gps.startSerial(9600);
  gps.setSentencesToReceive(OUTPUT_RMC_GGA);
  rotary_init(); // initialize the rotary
  // Set up the APRS module
  aprs_setup(AXFLAGS, // number of preamble flags (FLAG) (Hex: 0x7e, ASCII: ~, Binary: 01111110 )to send
             PTT_PIN, // Use PTT pin (=0 for VOX)
             AXDELAY, // ms to wait after PTT to transmit
             AXVOXON, AXVOXSILENT // VOX: tone length, silence length
            );

#define AX_DELAY     70
#define AX_FLAGS  72
#define AX_VOXTONE   74
#define AX_VOXSILENT 76
  // wait for a GPS sentence
  while (!(gps.sentenceAvailable())) {
    delay(1000);
  }
  gps.parseSentence();
  gps.dataRead();
  //  // only proced if the dates are valid
  //  while (gps.month <= 0 || gps.day <= 0 || gps.year <= 0 || gps.speed > 0) {
  //    if (gps.sentenceAvailable()) gps.parseSentence();
  //    if (gps.newValuesSinceDataRead())
  //      gps.dataRead();
  //    tft.print(".");
  //    delay(1000);
  //  }
  // now data should be stable. Get set and enter the loop
  timeOfAPRS = millis();
  sbSpeed   = gps.speed;  //prior read speed
  sbHeading = gps.heading;
  myHeading = sbHeading;
  broadcastLocation(gps, myComment);
  tft.fillScreen(ILI9341_BLACK);
  display();
}

////////////////////////////////////////////////////////////////////// loop()
void loop()
{
  // capture the button press
  if (pushbutton.update()) {
    if (pushbutton.fallingEdge()) {
      menu(addresses);
    }
  }
  //    unsigned char result = rotary_process(); // rotated?
  //    if (result) {
  //      if (result == DIR_CCW) {
  //        --mySpeed;
  //      }
  //      else {
  //        ++mySpeed;
  //      }
  //      Serial.println();
  //      Serial.print("mySpeed: ");
  //      Serial.println(mySpeed);
  //    }

  // get GPS data
  if (gps.sentenceAvailable()) gps.parseSentence();
  if (gps.newValuesSinceDataRead()) {         // go here as the seconds tick
    displayCountDown();
    gps.dataRead();
    if (sbEnable) {
      gotGPS = true; // prior validate idea... on the to do list
      mySpeed   = round(gps.speed * knotToMPH); // convert knots to MPH and store.
      myHeading = round(gps.heading);           // store the heading
      // SmartBeacon... my way
      if ((sbSpeed != mySpeed)) {               // was speed changed?????
        //go if something changed
        if ((mySpeed > sbSlowSpeed) && (mySpeed < sbFastSpeed)) {
          dTime = (( ( float(sbFastSpeed) / float(mySpeed) ) * (sbFastRate)) * 1000); // - (millis() - timeOfAPRS);
        }
        if (mySpeed >= sbFastSpeed) {         // are we above the fast level
          dTime = (sbFastRate * 1000);        // - (millis() - timeOfAPRS);
        }
      }
      if (sbHeading != myHeading) {             // was heading changed?????????
        uint16_t sbTurnThreshold = sbMinTurnAngle + (sbTurnSlope / mySpeed); // smartbeacon formula
        //  do we send beacon for a direction change??
        int dif = sbHeading - myHeading;
        dif = abs(dif);
        if (abs(dif) > 180) {
          dif = dif - 360;
        }
        dif = abs(dif);
        if (dif > sbTurnThreshold) {                              //did we trun enough?
          if (((millis() - timeOfAPRS) / 1000) > sbMinTurnTime) { //did enough time pass?
            timeOfAPRS = millis() - dTime;        // set the tme so we xmit
            sbHeading = myHeading;
          }
          else {
            Serial.println("not enuf time... reset");
            sbHeading = myHeading;              //not enough time so reset the heading and move on
          }
        }
      }
    }


    display();
    //Serial.printf("Sec: %d Heading %d Knots: %f\n\r", gps.seconds, gps.heading, gps.speed);
    //Serial.printf("Location: %f, %f Heading %d Knots: %f\n\r", gps.latitude, gps.longitude,gps.heading, gps.speed);
    //Serial.printf("dTime %i timeOf %i dT+timeOf %i millis %i\n\r", dTime, timeOfAPRS, dTime + timeOfAPRS, millis());
    //Serial.print((millis() - timeOfAPRS) / 1000);
  }
  // do we xmit?
  if (gotGPS && timeOfAPRS + dTime < millis()) {
    broadcastLocation(gps, myComment );
    timeOfAPRS = millis(); // reset the timer

    //Serial.printf("dTime: %d gps.speed: %f sbSpeed %f sbSlowrate: %d\n\r", dTime, gps.speed, sbSpeed , sbSlowRate);
    // SmartBeacon speed < low speed test and adjust as needed.
    if (sbEnable) {
      sbHeading = gps.heading; // reset the heading milestone.
      sbSpeed = gps.speed * knotToMPH; //store the new speed.
      if (gps.speed < sbSlowSpeed && sbSpeed < sbSlowSpeed && dTime < (sbSlowRate * 1000) ) {
        dTime = 2 * dTime;
        if (dTime > (sbSlowRate * 1000) ) dTime = sbSlowRate * 1000;
      }
    }
    display();
  }
} // Loop end YES the main loop ends here...
//
//
/////////////////////////////////////////////////////////////////////////////
//
//       FUNCTIONS from here down
//
////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////// menu()

void menu(const PathAddress *  paths) {
  {
    // lets set it up
    int mStart = 0;   //First menu item
    int mEnd   = 4 ;  // last menu item
    int mPick  = 0;   // menu choice
    int mB4    = 0;   // line # befor move
    //this defines the menu
    String menu1[] = {"Return", "Send/Return", "Packet", "AX.25", "SmartBeacon" };
    //now draw it
    tft.fillScreen(ILI9341_BLUE);
    tft.setTextSize(3);
    tft.setTextColor(ILI9341_WHITE, ILI9341_BLUE);
    tft.setCursor(0, 0);
    tft.println("Main  Menu");
    //tft.setCursor(160, 0);
    //tft.print("  ");
    //tft.setCursor(160, 0);
    //tft.print(mPick);
    for (int i = mStart; i <= mEnd; i++) {
      tft.setCursor(0, (i + 1) * 25);
      if (i == mPick) {
        tft.setTextColor(ILI9341_BLUE, ILI9341_WHITE);
      }
      else
      {
        tft.setTextColor(ILI9341_WHITE, ILI9341_BLUE);
      }
      //tft.print(String(i) + " ");
      tft.print(menu1[i]);
    }
    // now loop looking for a rotation or a button press
    while (true)
    {
      unsigned char result = rotary_process(); // rotated?
      if (result) {                            // the button was rotated
        if (result == DIR_CCW) {               // highlight the next choice
          if (--mPick < mStart) mPick = mEnd;
        }
        else {
          if (++mPick > mEnd) mPick = mStart;
        }
        Serial.printf("mPick: %i, mB4: %i\n\r", mPick, mB4);
        tft.setCursor(0, (mB4 + 1) * 25);
        tft.setTextColor(ILI9341_WHITE, ILI9341_BLUE);
        //tft.print(String(mB4) + " ");
        tft.print(menu1[mB4]);
        tft.setCursor(0, (mPick + 1) * 25);
        tft.setTextColor(ILI9341_BLUE, ILI9341_WHITE);
        //tft.print(String(mPick) + " ");
        tft.print(menu1[mPick]);
        tft.setTextColor(ILI9341_WHITE, ILI9341_BLUE);
        mB4 = mPick;
      }
      if (pushbutton.update()) {                // button pushed
        if (pushbutton.fallingEdge()) {
          switch (mPick) {                      //handle the button push
            case 0:  // return
              tft.fillScreen(ILI9341_BLACK);
              display();
              return;
            case 1:  // send and return
              gps.dataRead();
              broadcastLocation(gps, myComment );
              tft.fillScreen(ILI9341_BLACK);
              timeOfAPRS = millis();
              display();
              return;
            case 2: // sendMenu
              packetMenu(paths);
              return;
            case 3: // sendMenu
              ax25Menu();
              return;
            case 4: // SmartBeacon Menu
              sbMenu();
              return;
          } // switch end
        }   // if (pushbutton.update())
      }     // if (pushbutton.fallingEdge())
    }       // while (true) end
  }         // end of menu function
}

/////////////////////////////////////////////////////////// AX25Menu()
void ax25Menu()
{
  // lets set it up
  int mStart = 0;   //First menu item
  int mEnd   = 5 ;  // last menu item
  int mPick  = 0;   // menu choice
  int mB4    = 0;   // line # before move

  String maxDelay      = "Xmit Delay:" + String(axDelay);
  String maxFlags      = "# of Flags:" + String(axFlags);
  String maxVoxOn      = "VOX on:" + String(axVoxOn);
  String maxVoxSilent  = "VOX off :" + String(axVoxSilent);
  String mpttPin       = "PTT Pin:" + String(pttPin);

  String menu1[] = {"Return", maxDelay, maxFlags, maxVoxOn, maxVoxSilent, mpttPin };
  //now draw it
  tft.fillScreen(ILI9341_CYAN);
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_BLACK, ILI9341_CYAN);
  tft.setCursor(0, 0);
  tft.println("AX.25 Menu");
  for (int i = mStart; i <= mEnd; i++) {
    tft.setCursor(0, (i + 1) * 25);
    if (i == mPick) {
      tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
    }
    else
    {
      tft.setTextColor(ILI9341_BLACK, ILI9341_CYAN);
    }
    //tft.print(String(i) + " ");
    tft.print(menu1[i]);
  }
  // now loop looking for a rotation or a button press
  while (true)
  {
    unsigned char result = rotary_process(); // rotated?
    if (result) {                            // the button was rotated
      if (result == DIR_CCW) {               // highlight the next choice
        if (--mPick < mStart) mPick = mEnd;
      }
      else {
        if (++mPick > mEnd) mPick = mStart;
      }
      Serial.printf("mPick: %i, mB4: %i\n\r", mPick, mB4);
      tft.setCursor(0, (mB4 + 1) * 25);
      tft.setTextColor(ILI9341_BLACK, ILI9341_CYAN);
      //tft.print(String(mB4) + " ");
      tft.print(menu1[mB4]);
      tft.setCursor(0, (mPick + 1) * 25);
      tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
      //tft.print(String(mPick) + " ");
      tft.print(menu1[mPick]);
      tft.setTextColor(ILI9341_BLACK, ILI9341_CYAN);
      mB4 = mPick;
    }
    if (pushbutton.update()) {                // button pushed
      if (pushbutton.fallingEdge()) {
        switch (mPick) {                      //handle the button push
          // this will use the uint16_t options so arg 2 is a dummy and arg 8 just need a non-zero value.
          case 0:  // return
            tft.fillScreen(ILI9341_BLACK);
            display();
            return;
          case 1: // xmit delay
            mNumChoice(AXDELAY, &TimeZone , 10, 1000, axDelay , String("Xmit Delay"), "Xmit Delay", axDelay, &axDelay);
            return;
          case 2: // flags
            mNumChoice(AXFLAGS, &TimeZone , 1, 100, axFlags , String("Flags"), "Flags", axFlags, &axFlags);
            return;
          case 4:
            mNumChoice(AXVOXON, &TimeZone , 0, 1000, axVoxOn , String("VOX On"), "VOX On", axVoxOn, &axVoxOn);
            return;
          case 5:
            mNumChoice(AXVOXSILENT, &TimeZone , 0, 1000, axVoxSilent , String("VOX Silent"), "VOX Silent", axVoxSilent, &axVoxSilent);
            return;
          case 6:
            mNumChoice(PTT_PIN , &TimeZone , 0, 23, pttPin , String("PTT Pin #"), "PTT Pin", pttPin, &pttPin);
            return;

        } // switch end
      }   // if (pushbutton.update())
    }     // if (pushbutton.fallingEdge())
  }       // while (true) end
}         // end of menu function



/////////////////////////////////////////////////////////// sbMenu()
void sbMenu()
{
  // lets set it up
  int mStart = 0;   //First menu item
  int mEnd   = 8 ;  // last menu item
  int mPick  = 0;   // menu choice
  int mB4    = 0;   // line # before move
  String enabled;
  if (sbEnable) {
    enabled     = "Enabled";
  }
  else {
    enabled     = "Disabled";
  }
  String fastSpeed  = "fSpd :" + String(sbFastSpeed);
  String fastRate   = "fRate:" + String(sbFastRate);
  String slowSpeed  = "sSpd :" + String(sbSlowSpeed);
  String slowRate   = "sRate:" + String(sbSlowRate);
  String mTurnTime  = "tTime:" + String(sbMinTurnTime);
  String mTurnAngle = "tAgle:" + String(sbMinTurnAngle);
  String turnSlope  = "Slope:" + String(sbTurnSlope);
  String menu1[] = {"Return", enabled, fastSpeed, fastRate, slowSpeed, slowRate, mTurnTime, mTurnAngle, turnSlope };
  //now draw it
  tft.fillScreen(ILI9341_CYAN);
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_BLACK, ILI9341_CYAN);
  tft.setCursor(0, 0);
  tft.println("SB Menu");
  for (int i = mStart; i <= mEnd; i++) {
    tft.setCursor(0, (i + 1) * 25);
    if (i == mPick) {
      tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
    }
    else
    {
      tft.setTextColor(ILI9341_BLACK, ILI9341_CYAN);
    }
    //tft.print(String(i) + " ");
    tft.print(menu1[i]);
  }
  // now loop looking for a rotation or a button press
  while (true)
  {
    unsigned char result = rotary_process(); // rotated?
    if (result) {                            // the button was rotated
      if (result == DIR_CCW) {               // highlight the next choice
        if (--mPick < mStart) mPick = mEnd;
      }
      else {
        if (++mPick > mEnd) mPick = mStart;
      }
      Serial.printf("mPick: %i, mB4: %i\n\r", mPick, mB4);
      tft.setCursor(0, (mB4 + 1) * 25);
      tft.setTextColor(ILI9341_BLACK, ILI9341_CYAN);
      //tft.print(String(mB4) + " ");
      tft.print(menu1[mB4]);
      tft.setCursor(0, (mPick + 1) * 25);
      tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
      //tft.print(String(mPick) + " ");
      tft.print(menu1[mPick]);
      tft.setTextColor(ILI9341_BLACK, ILI9341_CYAN);
      mB4 = mPick;
    }
    if (pushbutton.update()) {                // button pushed
      if (pushbutton.fallingEdge()) {
        switch (mPick) {                      //handle the button push
          // this will use the uint16_t options so arg 2 is a dummy and arg 8 just need a non-zero value.
          case 0:  // return
            tft.fillScreen(ILI9341_BLACK);
            display();
            return;
          case 1: // Enabled
            mNumChoice(SB_ENABLE, &sbEnable , 0, 1, sbEnable , String("SB Enabled"), "Smartbeacon");
            if (!sbEnable) {
              sTime = EEPROM.readInt(XMIT_TIME); // seconds
              dTime = sTime * 1000; // store the start time into dTime (delay) in milliseconds
              timeOfAPRS = millis();
              Serial.print("dTime:");
              Serial.println(dTime);
            }
            return;
          case 2: // FastSpeed
            mNumChoice(SBFAST_SPEED, &TimeZone , 10, 100, sbFastSpeed , String("Fast Speed"), "Smartbeacon", sbFastSpeed, &sbFastSpeed);
            return;
          case 3:
            mNumChoice(SBFAST_RATE, &TimeZone , 10, 600, sbFastRate , String("Fast Rate"), "Smartbeacon", sbFastRate, &sbFastRate);
            return;
          case 4:
            mNumChoice(SBSLOW_SPEED, &TimeZone , 1, 20, sbSlowSpeed , String("Slow Speed"), "Smartbeacon", sbSlowSpeed, &sbSlowSpeed);
            return;
          case 5:
            mNumChoice(SBSLOW_RATE, &TimeZone , 1000, 5000, sbSlowRate , String("Slow Rate"), "Smartbeacon", sbSlowRate, &sbSlowRate);
            return;
          case 6:
            mNumChoice(SBTURN_TIME, &TimeZone , 5, 30, sbMinTurnTime , String("Min. Turn Time"), "Smartbeacon", sbMinTurnTime, &sbMinTurnTime);
            return;
          case 7:
            mNumChoice(SBTURN_ANGLE, &TimeZone , 5, 30, sbMinTurnAngle , String("Min. Turn Angle"), "Smartbeacon", sbMinTurnAngle, &sbMinTurnAngle);
            return;
          case 8: // SmartBeacon Menu
            mNumChoice(SBTURN_SLOPE, &TimeZone , 200, 300, sbTurnSlope , String("Turn Slope"), "Smartbeacon", sbTurnSlope, &sbTurnSlope);
            return;
        } // switch end
      }   // if (pushbutton.update())
    }     // if (pushbutton.fallingEdge())
  }       // while (true) end
}         // end of menu function

///////////////////////////////////////////////////////////////////// packetMenu()

void packetMenu(const PathAddress *  paths)
{
  // lets set it up
  int mStart = 0;   //First menu item
  int mEnd   = 8;   // last menu item
  int mPick  = 0;   // menu choice
  int mB4    = 0;   // line # befor move
  String symName;   // symbol name holder
  int symNum = 0;   // curnt symbol # in array
  //this defines the menu

  for (int i = 0; i < NUM_SYMBOLS; i++) { // find the symbol, store the name into symName
    if ((symbol == symbols[i][2][0]) && (symTable == symbols[i][1][0]) ) {
      symName   = String(symbols[i][0]);
      symNum = i;
      break;
    }
  }
  String utcOffSet = "UTC:  " + String(TimeZone);
  String xmitDelay = "Delay:" + String(sTime);
  String ssid1     = "SSID: " + String(paths[1].ssid);
  String ssid0     = "SSID: " + String(paths[0].ssid);
  String menu1[] = {"Return", utcOffSet, xmitDelay, paths[1].callsign, ssid1, paths[0].callsign, ssid0, symName, "Message" };
  //now draw it
  tft.fillScreen(ILI9341_BLUE);
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLUE);
  tft.setCursor(0, 0);
  tft.println("Packet Menu");
  //tft.setCursor(160, 0);
  //tft.print("  ");
  //tft.setCursor(160, 0);
  //tft.print(mPick);
  for (int i = mStart; i <= mEnd; i++) {
    tft.setCursor(0, (i + 1) * 25);
    if (i == mPick) {
      tft.setTextColor(ILI9341_BLUE, ILI9341_WHITE);
    }
    else
    {
      tft.setTextColor(ILI9341_WHITE, ILI9341_BLUE);
    }
    //tft.print(String(i) + " ");
    tft.print(menu1[i]);
  }
  // now loop looking for a rotation or a button press
  while (true)
  {
    unsigned char result = rotary_process(); // rotated?
    if (result) {                            // the button was rotated
      if (result == DIR_CCW) {               // highlight the next choice
        if (--mPick < mStart) mPick = mEnd;
      }
      else {
        if (++mPick > mEnd) mPick = mStart;
      }
      //tft.setCursor(160, 0);                 // draw it
      //tft.print("  ");
      //tft.setCursor(160, 0);
      //tft.print(mPick);
      Serial.printf("mPick: %i, mB4: %i\n\r", mPick, mB4);
      tft.setCursor(0, (mB4 + 1) * 25);
      tft.setTextColor(ILI9341_WHITE, ILI9341_BLUE);
      //tft.print(String(mB4) + " ");
      tft.print(menu1[mB4]);
      tft.setCursor(0, (mPick + 1) * 25);
      tft.setTextColor(ILI9341_BLUE, ILI9341_WHITE);
      //tft.print(String(mPick) + " ");
      tft.print(menu1[mPick]);
      tft.setTextColor(ILI9341_WHITE, ILI9341_BLUE);
      mB4 = mPick;
    }
    if (pushbutton.update()) {                // button pushed
      if (pushbutton.fallingEdge()) {
        switch (mPick) {                      //handle the button push
          case 0:  // return
            tft.fillScreen(ILI9341_BLACK);
            display();
            return;
          case 1: // utc offset set
            mNumChoice(UTC_OFFSET, &TimeZone, -12, 14, TimeZone, "UTC Offset", "Set to actual offset including DST");
            return;
          case 2: // xmit delay
            mNumChoice(XMIT_TIME, &TimeZone , 10, 600, sTime , String("Xmit Delay"), "Seconds between transissions (10-600)", sTime, &sTime );
            dTime = sTime * 1000;
            return;
          case 3:
            mCallChoice("My Call:", sCall) ;
            return;
          case 4:
            // same format as 7
            mNumChoice(MY_SSID, &addresses[1].ssid, 0, 15, addresses[1].ssid , String(paths[1].callsign) + "-SSID", "SSID for:" + String(paths[1].callsign) );
            return;
          case 5:
            mCallChoice("Dest. Call:", dCall) ;
            return;
          case 6:
            mNumChoice(DEST_SSID,
                       &addresses[0].ssid,
                       0,
                       15,
                       addresses[0].ssid ,
                       String(paths[0].callsign) + "-SSID",
                       "SSID for:" + String(paths[0].callsign) );
            return;
          case 7:
            mSymChoice("Symbol", symName, symNum);
            return;
          case 8: // this will do the message
            //mSymChoice("Symbol", symName, symNum);
            return;
        } // switch end
      }   // if (pushbutton.update())
    }     // if (pushbutton.fallingEdge())
  }       // while (true) end
}         // end of menu function

////////////////////////////////////////////////////////////mSymChoice
void mSymChoice(String title, String sNameNow, int sNumNow  ) {
  /*
   * Menu choice for the symbols....
   *
   */
  // this is the setup()

  tft.fillScreen(ILI9341_BLUE);     // now draw the screen
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLUE);
  tft.setCursor(0, 0);
  tft.println(title);
  tft.setCursor(0, 25);
  tft.print("Now: ");
  tft.setCursor(80, 25);
  tft.print(sNameNow);
  tft.setCursor(0, 75);
  tft.print("New: ");
  tft.setCursor(80, 75);
  tft.setTextColor( ILI9341_BLUE, ILI9341_WHITE);
  tft.print(symbols[sNumNow][0]);
  int sNumNew = sNumNow;
  while (true) {
    if (pushbutton.update()) {                // button pushed
      if (pushbutton.fallingEdge()) {
        if (sNumNew != sNumNow) {      // we have a new symbol to save
          Serial.print(symbols[sNumNew][1]);
          Serial.print(symbols[sNumNew][2]);
          Serial.println(symbols[sNumNew][0]);
          EEPROM.update(SYM_TABLE, symbols[sNumNew][1][0]);
          EEPROM.update(SYMBOL, symbols[sNumNew][2][0]);
          symTable = symbols[sNumNew][1][0];
          symbol = symbols[sNumNew][2][0];
          Serial.println(symTable);
          Serial.print(symbol);
        }
        tft.fillScreen(ILI9341_BLACK);
        display();
        return;

      }
    }
    unsigned char result = rotary_process(); // rotated?
    if (result) {
      if (result == DIR_CCW) {
        --sNumNew;
        if (sNumNew < 0 ) sNumNew = NUM_SYMBOLS - 1;
      }
      else {
        ++sNumNew;
        if (sNumNew >= NUM_SYMBOLS ) sNumNew = 0;
      }
      tft.setCursor(80, 75 );
      tft.setTextColor( ILI9341_WHITE, ILI9341_BLUE);
      tft.print( "           " );
      tft.setCursor(80, 75 );
      tft.setTextColor( ILI9341_BLUE, ILI9341_WHITE);
      tft.print(symbols[sNumNew][0]);
    }
  }
}
////////////////////////////////////////////////////////////mCallChoice
void mCallChoice(String title, char *vnow) {
  /*
   * Menu choice for the two call signs....
   *
   */
  // this is the setup()
  char alpha[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890 ";  // Alpha array to make the call
  char cNew[7]; //"123456";   // holds new call
  strcpy(cNew, vnow);
  String mCallExit[] = {"Continue", "Exit", "Exit/Save"};  // exit choices
  int nLetter = 0;                  // pointer to current letter in new call
  int mCallExitChoice = 0;          // exit choice pointer
  int8_t letter = 0;                // ptr for the alpha[] array when rotating.
  tft.fillScreen(ILI9341_BLUE);     // now draw the screen
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLUE);
  tft.setCursor(0, 0);
  tft.println(title);
  tft.setCursor(0, 25);
  tft.print("Now: ");
  tft.print(vnow);
  tft.setCursor(0, 75);
  tft.print("New: ");
  tft.setCursor(90, 75);
  for (int i = 0; i < 6; i++) {  // this is a predraw before we enter the loop
    if (i == nLetter) {
      tft.setTextColor(ILI9341_YELLOW, ILI9341_BLUE);
      tft.print(cNew[i]);
      tft.setTextColor(ILI9341_BLUE, ILI9341_WHITE);
    }
    else {
      tft.print(cNew[i]);
    }
  }
  // this is the loop() for the call editor
  while (true) {
    if (pushbutton.update()) {                // button pushed
      if (pushbutton.fallingEdge()) {
        nLetter++;
        if (nLetter > 5) { // beyond the call lets do the exit menu
          nLetter = 0;     // reset new call pointer
          tft.setCursor(10, 125); // display exit options
          tft.print( "         " );
          tft.setCursor(10, 125);
          tft.print( mCallExit[mCallExitChoice]);
          while (true) {
            unsigned char result = rotary_process(); // rotated?
            if (result) {
              if (++mCallExitChoice > 2) mCallExitChoice = 0;
              tft.setCursor(10, 125); // display exit options
              tft.setTextColor( ILI9341_WHITE, ILI9341_BLUE);
              tft.print( "         " );
              tft.setTextColor( ILI9341_BLUE, ILI9341_WHITE);
              tft.setCursor(10, 125);
              tft.print( mCallExit[mCallExitChoice]);
            }
            if (pushbutton.update()) {                // button pushed
              if (pushbutton.fallingEdge()) {
                switch (mCallExitChoice) {
                  case 0: //continue
                    tft.setCursor(10, 125);
                    tft.setTextColor( ILI9341_WHITE, ILI9341_BLUE);
                    tft.print( "         " );
                    break; //breaks the switch
                  case 1: // exit
                    tft.fillScreen(ILI9341_BLACK);
                    display();
                    return;
                  case 2: //exit and save
                    strcpy(vnow, cNew);
                    tft.fillScreen(ILI9341_BLACK);
                    display();
                    return;
                } // switch end
                break;  // breaks the exit wile true loop
              } // falling edge if
            }   // pb update if
          }     // while true loop for exit
        }       // if need to go into end loop if
      }         // pb if while scanning call
      tft.setCursor(90, 75);
      for (int i = 0; i < 6; i++) {
        if (i == nLetter) {
          tft.setTextColor(ILI9341_YELLOW, ILI9341_BLUE);
          tft.print(cNew[i]);
          tft.setTextColor(ILI9341_BLUE, ILI9341_WHITE);
        }
        else {
          tft.print(cNew[i]);
        } // if / else end
      }   // for loop en
    }     // PB end.. from way up there
    // find the existing call letter in the alpha array so the rotation starts there!
    for (int i = 0; i < 37; i++) {
      if (cNew[nLetter] == alpha[i]) {
        letter = i; // found it, set the letter to is and get out of the for loop
        break;
      }
    }
    unsigned char result = rotary_process(); // rotated?
    if (result) {
      if (result == DIR_CCW) {
        --letter;
        if (letter < 0) letter = 37;
      }
      else {
        ++letter;
        if (letter > 37) letter = 0;
      }
      cNew[nLetter] = alpha[letter];
      tft.setCursor(90, 75);
      for (int i = 0; i < 6; i++) {
        if (i == nLetter) {
          tft.setTextColor(ILI9341_YELLOW, ILI9341_BLUE);
          tft.print(cNew[i]);
          tft.setTextColor(ILI9341_BLUE, ILI9341_WHITE);
        }
        else {
          tft.print(cNew[i]);
        } // if/else end
      }   // for loop end printing the call
    }     // if(result) end
  }       // outer while true loop
}         // mCallChoice() end


////////////////////////////////////////////////////////////mNumChoice
void mNumChoice(int eePromAddress
                , int8_t *variable  // pointer to the int8_t variable to be edited
                , int lTZ           // lower choice limit
                , int uTZ           // upper choice limit
                , int nTZ           // this is the current value that is dispalyed
                , String title      // size 3 at top of screen
                , String help       //size 2 below input area
                , uint16_t TTT      // optional current value non-zero for trigger
                , uint16_t *variable2) // opt. pointer to uint32_t value to be edited
{
  /*
   * Menu choice for numeric entries entries
   * Pararmaters:
   *  EEPROM address
   *  *variable pts to the variable we are changing
   *  dTime =0 to skip
   *  lTZ lower limit
   *  uTZ upper limit
   *  nTZ current value
   *  title top of menu item 13 chars max
   *  help can be multi-line string. 20 chars / line
   *  TTT optional current time in second !=0 for dTime delay
   *  * tz, SIIDs are signed 8 bit, xmit delay time is 16 (int())
   *  *variable optional points to dTime
   */
  tft.fillScreen(ILI9341_BLUE);
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLUE);
  tft.setCursor(0, 0);
  tft.println(title);
  tft.setCursor(0, 25);
  tft.print("Now: ");
  if (TTT != 0) {
    tft.print(*variable2);
  }
  else {
    tft.print(*variable);
  }
  tft.setCursor(0, 75);
  tft.print("New: ");
  tft.setCursor(0, 125);
  tft.setTextSize(2);
  tft.println("Rotate to New Value");
  tft.println("Push to accept");
  tft.println("");
  tft.println(help);
  tft.setTextSize(3);
  tft.setCursor(100, 75);
  tft.setTextColor(ILI9341_BLUE, ILI9341_WHITE);
  tft.print(nTZ);
  while (true) {
    if (pushbutton.update()) {                // button pushed
      if (pushbutton.fallingEdge()) {
        if (TTT != 0) {                      // this is the int16_t calcs.
          if (*variable2 != nTZ) {           // *variable2 points to dTime
            *variable2 = nTZ;
            EEPROM.updateInt(eePromAddress, *variable2);
            *variable2 = *variable2;
          }
        }
        else {                                // this is the int8_t calcs
          if (*variable != nTZ) {
            *variable = nTZ;
            EEPROM.update(eePromAddress, *variable);
          }
        }
        tft.fillScreen(ILI9341_BLACK);
        display();
        return;
      }
    }
    unsigned char result = rotary_process(); // rotated?
    if (result) {
      if (result == DIR_CCW) {
        if (--nTZ < lTZ) {
          nTZ = uTZ;
        }
      }
      else {
        if (++nTZ > uTZ) {
          nTZ = lTZ;
        }
      }
      //tft.setTextColor(ILI9341_WHITE,ILI9341_BLUE);
      //tft.setCursor(0, 75);
      //tft.print("New: ");
      tft.setCursor(100, 75);
      tft.setTextColor(ILI9341_WHITE, ILI9341_BLUE);
      tft.print("   ");
      tft.setTextColor(ILI9341_BLUE, ILI9341_WHITE);
      tft.setCursor(100, 75);
      tft.print(nTZ);
    }
  }
  delay(1000);
  tft.fillScreen(ILI9341_BLACK);
  display();
  return;
}

//////////////////////////////////////////////////////////// displayCountDown()
void displayCountDown()
{
  tft.setTextSize(2);
  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  tft.setCursor(170, 0);
  uint32_t tLeft = (dTime - (millis() - timeOfAPRS)) / 1000;
  int sec = tLeft % 60;
  int min = tLeft / 60;
  char buf[6];
  sprintf( buf, "%2d:%2d", min, sec);
  tft.print(buf);
  tft.setCursor(170, line * 1);
  tLeft = dTime / 1000;
  sec = tLeft % 60;
  min = tLeft / 60;
  sprintf( buf, "%2d:%2d", min, sec);
  tft.print(buf);
}
///////////////////////////////////////////////////////////////// display()
void display()
{
  tft.setTextSize(3);
  tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK);
  tft.setCursor(0, 0);
  gpsYear  = gps.year;
  gpsMonth = gps.month;
  gpsDay   = gps.day;
  gpsHour  = gps.hour;
  gpsHour += TimeZone; // Time zone correction
  if (gpsHour < 0)
  {
    gpsHour += 24;
    gpsDay -= 1;
    if (gpsDay < 1)
    {
      if (gpsMonth == 1)
      {
        gpsMonth = 12;
        gpsYear -= 1;
      }
      else
      {
        gpsMonth -= 1;
      }
      gpsDay = DaysAMonth[gpsMonth - 1];
    }
  }
  if (gpsHour >= 24)
  {
    gpsHour -= 24;
    gpsDay += 1;
    if (gpsDay > DaysAMonth[gpsMonth - 1])
    {
      gpsDay = 1;
      gpsMonth += 1;
      if (gpsMonth > 12) gpsYear += 1;
    }
  }
  char sz[32];
  sprintf(sz, "%02d/%02d/%02d ", gpsMonth, gpsDay, gpsYear);
  tft.println(sz);
  //char sz[32];
  sprintf(sz, "%02d:%02d ", gpsHour, gps.minute);
  tft.println(sz);

  tft.setTextSize(3);
  tft.setCursor(0, line * 3);
  tft.setTextColor(ILI9341_GREEN, ILI9341_BLACK);
  displayLatLong(gps.latitude);
  tft.setTextSize(2);
  if (gps.latitude < 0)
  {
    tft.println(" S");
  }
  else
  {
    tft.println(" N");
  }
  tft.setTextSize(3);
  tft.setCursor(0, line * 5);
  tft.setTextColor(ILI9341_YELLOW, ILI9341_BLACK);
  displayLatLong(gps.longitude);
  tft.setTextSize(2);
  if (gps.longitude < 0)
  {
    tft.println(" W");
  }
  else
  {
    tft.println(" E");
  }
  tft.setTextSize(3);
  tft.setCursor(60, line * 7);
  tft.setTextColor(ILI9341_MAGENTA, ILI9341_BLACK);
  tft.setTextSize(4);
  char buf[6];
  //sprintf( buf, "%2d MPH", int(mySpeed ));// for testing
  sprintf( buf, "%2d MPH", int(round(gps.speed * knotToMPH) ));
  tft.print(buf);
  tft.setCursor(40, line * 9);
  tft.setTextSize(4);
  tft.setTextColor(ILI9341_CYAN, ILI9341_BLACK);
  sprintf( buf, "%3d deg", int(round(gps.heading) ));
  tft.print(buf);
  tft.setCursor(0, 280);
  tft.setTextSize(2);
  printStr("Sats:", 6, false);
  printStr(String(gps.satellites), 2, false);
  tft.setCursor(150, 280);
  float x = analogRead(39);
  if (x <= 1500) // approximately 3 vdc
  {
    tft.setTextColor(ILI9341_GREEN);
    tft.print("V+ OK");
  }
  else
  {
    tft.setTextColor(ILI9341_RED);
    tft.print("V+ ");
    tft.println(( (178 * x * x + 2688757565 - 1184375 * x)  / 372346 ) / 1000);
  }
  tft.setCursor(170, 300);
  tft.setTextColor(ILI9341_GREEN);
  tft.setTextSize(1);
  printStr(thisver, 6, false); // display version

}
/////////////////////////////////////////////////////////////// displayLatLong()
static void displayLatLong(float val)
// converts decimal degrees to degrees and decimal minutes (APRS format)
{
  float wlong = val;
  char charVal[10];
  int deglong = wlong; //long;
  wlong -= deglong; // remove the degrees from the calculation
  wlong *= 60; // convert to minutes
  float mlong = wlong;
  String pDegrees = " " + String(abs(deglong));
  //return pDegrees;
  printStr(pDegrees, 4, false);
  tft.setTextSize(2);
  tft.print(char(247));
  tft.setTextSize(3);
  mlong = abs(mlong);
  dtostrf(mlong, 6, 4, charVal);
  pDegrees = "";

  for (unsigned int i = 0; i < sizeof(charVal); i++) // was sizeof
  {
    pDegrees += charVal[i];
  }
  if (mlong < 10)
  {
    printStr(" ", 2, false);
    printStr(pDegrees, 5, false);
  }
  else
  {
    printStr(" ", 1, false);
    printStr(pDegrees, 6, false);
  }
}

///////////////////////////////////////////////////////////////// printStr()
static void printStr(String istring, unsigned int len, boolean rtn)
{

  String sout = "";
  unsigned int slen = istring.length();// this how long it is
  istring = istring.trim();
  if (slen > len)
  {
    sout = istring.substring(0, len);
  }
  else
  {
    sout = istring;
    while (sout.length() < len)
    {
      sout = " " + sout;
    }
  }
  if (rtn)
  {
    tft.println(sout);
  }
  else
  {
    tft.print(sout);
  }
}


/////////////////////////////////////////////////////////////////////// broadcastLocation()
//Function to broadcast your location
void broadcastLocation(GPS &gps, const char *bcomment)
{
  // If above 5000 feet switch to a single hop path
  int nAddresses;
  // load the user data variables into the addresses[] array for xmit
  //
  addresses[0].callsign = dCall;
  addresses[0].ssid = EEPROM.read(DEST_SSID);
  addresses[1].callsign = sCall;
  addresses[1].ssid = EEPROM.read(MY_SSID);
  symbol =  EEPROM.read(SYMBOL);
  symTable =  EEPROM.read(SYM_TABLE);
  if (gps.altitude > 1500) {
    // APRS recomendations for > 5000 feet is:
    // Path: WIDE2-1 is acceptable, but no path is preferred.
    nAddresses = 3;
    addresses[2].callsign = "WIDE2";
    addresses[2].ssid = 1;
  } else {
    // Below 1500 meters use a much more generous path (assuming a mobile station)
    // Path is "WIDE1-1,WIDE2-2"
    nAddresses = 4;
    addresses[2].callsign = "WIDE1";
    addresses[2].ssid = 1;
    addresses[3].callsign = "WIDE2";
    addresses[3].ssid = 2;
  }
  // For debugging print out the path
  Serial.print("APRS(");
  Serial.print(nAddresses);
  Serial.print("): ");
  for (int i = 0; i < nAddresses; i++) {
    Serial.print(addresses[i].callsign);
    Serial.print('-');
    Serial.print(addresses[i].ssid);
    if (i < nAddresses - 1)
      Serial.print(',');
  }
  Serial.print(' ');
  Serial.print(symTable);
  Serial.print(symbol);
  Serial.println();
  // Send the packet
  aprs_send(addresses, nAddresses
            , gps.day, gps.hour, gps.minute
            , gps.latitude, gps.longitude // degrees
            , gps.altitude // meters
            , gps.heading
            , gps.speed
            , symTable //SYMBOL_TABLE
            , symbol //SYMBOL_CHAR
            , bcomment);
  //Serial.print("APRS sent");
  //Serial.printf("Location: %f, %f altitude %f\n\r", gps.latitude, gps.longitude, gps.altitude);
}

