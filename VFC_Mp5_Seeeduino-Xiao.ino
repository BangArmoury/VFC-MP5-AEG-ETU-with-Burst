#include <Arduino.h>
#include "Bounce2.h"
#include "interrupt.h"
#include <ArduinoLowPower.h>        // Energiesparmaßnahmen für Cortex M0/+ (Arduino Zero (SAMD21))


/*******************************************************************************
  Settings
*******************************************************************************/

#define ENABLE_BURST 1      // Uncomment to enable burst fire
#define ENABLE_FULLAUTO 1   // Uncomment to enable full auto
#define DEBUG 1             // Debug Mode

#define BURST_CNT 3         // Set Burst here

#define BAUD 115200         //Übertragungsgeschwindigkeit in kb/s
/*******************************************************************************
  Pins
*******************************************************************************/

// set according your setup
#define PIN_SEL 7       // analog anglesensor
#define PIN_COL 8       // COL                 
#define PIN_FET 10      // MosFet Gate 
#define PIN_TRG 9       // Trigger

/*******************************************************************************
  Global constants
*******************************************************************************/

// only touch when knowing what to do

#define DEB_TRG 50      // debounce time for trigger
#define DEB_COL 2       // debounce time for COL
#define MAX_CYC 60      // maximum cycle time default 500

#define RPM_LIM 600     // RPM Limit - 0=unlimited - edit here --> for MP5 is 800 rpm

Bounce colBouncer = Bounce();

/*******************************************************************************
  Global variables
*******************************************************************************/
//do not touch
bool triggerPressed = false;
int cycleLength = 0;
int rpmDelay = 0;
long lastTrigger = -1;
int errorCnt = 0;
bool emergencymode = false;
long lasterror = -1;

//New variables
int Firemode = 0;

int SafeMid = 0;
int SafeLow = 0;
int SafeHigh = 0;

int SemiMid = 0;
int SemiLow = 0;
int SemiHigh = 0;

int BurstMid = 0;
int BurstLow = 0;
int BurstHigh = 0;

int AutoMid = 0;
int AutoLow = 0;
int AutoHigh = 0;

int Toleranz = 10;             // tolerance value for the anglesensor, increase the value if the mechanical system has to much clearance!

/*******************************************************************************
                                      Setup
*******************************************************************************/

void setup() {
  delay(3000);
#ifdef DEBUG
  Serial.begin(BAUD);
  delay(1000);
  Serial.println("Board booted up and now setting up pins");
#endif // DEBUG

  lastTrigger = millis();
  triggerPressed = false;

  //Pin setup
  pinMode(PIN_TRG, INPUT_PULLUP);
  pinMode(PIN_COL, INPUT_PULLUP);
  pinMode(PIN_FET, OUTPUT);
  //pinMode(PIN_SEL, INPUT);              // pinMode for AnalogRead not necessary!

  // if we had an error
  if (errorCnt > 0) {
#ifdef DEBUG
    Serial.println("An error occured");
#endif // DEBUG
    errorCnt = 0;
  }

  /*******************************************************************************
    Fireselector initialization
   *******************************************************************************/

  delay(1000);                             // Fireselector on Safe and then plug in the accumulator!
  SafeMid = analogRead(PIN_SEL);           // current value will be saved for Safe mode
  delay(1000);                             // Wait one second, for debouncing reasons...

  while  (digitalRead(PIN_TRG) != LOW) {} { // Switch to Semi and pull the Trigger, same method for the other firemodes
    SemiMid = analogRead(PIN_SEL);
  }
  delay(1000);

  while  (digitalRead(PIN_TRG) != LOW) {} {
    BurstMid = analogRead(PIN_SEL);
  }
  delay(1000);

  while  (digitalRead(PIN_TRG) != LOW) {} {
    AutoMid = analogRead(PIN_SEL);
    lastTrigger = millis();              //setting lastTrigger
  }
  delay(1000);


#ifdef DEBUG
  Serial.print("SafeMid: ");               // Checking if the anglesensor is working correctly
  Serial.println(SafeMid);
  Serial.print("SemiMid: ");
  Serial.println(SemiMid);
  Serial.print("BurstMid: ");
  Serial.println(BurstMid);
  Serial.print("AutoMid: ");
  Serial.println(AutoMid);
#endif //DEBUG

  delay(1000);

  // Safe                                 //Calculation of the lower and higher limits for the fire modes.
  SafeLow = SafeMid - Toleranz;
  SafeHigh = SafeMid + Toleranz;

  // Semi
  SemiLow = SemiMid - Toleranz;
  SemiHigh = SemiMid + Toleranz;

  // Burst
  BurstLow = BurstMid - Toleranz;
  BurstHigh = BurstMid + Toleranz;

  // Auto
  AutoLow = AutoMid - Toleranz;
  AutoHigh = AutoMid + Toleranz;


#ifdef DEBUG
  Serial.print("SafeHigh: ");
  Serial.println(SafeHigh);

  Serial.print("SemiHigh: ");
  Serial.println(SemiHigh);

  Serial.print("BurstHigh: ");
  Serial.println(BurstHigh);

  Serial.print("AutoHigh: ");
  Serial.println(AutoHigh);
#endif //DEBUG

  delay(1000);
  // End of fireselector initialization

  colBouncer.attach(PIN_COL);
  colBouncer.interval(DEB_COL);

  //initialize Pins with LOW
  digitalWrite(PIN_FET, LOW);
  attachInterrupt(PIN_TRG, isr_fire, FALLING);

  // calculate RPM
  // 60000ms(=1min) / RPM
  if (RPM_LIM > 0) {
    rpmDelay = 60000 / RPM_LIM;
  }

#ifdef DEBUG
  Serial.print("rpmDelay: ");
  Serial.println(rpmDelay);
  Serial.print("RPM Limit: ");
  Serial.println(RPM_LIM);
#endif // DEBUG
}

/*******************************************************************************
  Functions
*******************************************************************************/

int F_Firemode()                                  //subroutine for firemode
{
  int x = 0;
  x = analogRead(PIN_SEL);

  if (x > SemiLow & x < SemiHigh)  {
    Firemode = 1;                                 // Semi
  }
  else if (x > BurstLow & x < BurstHigh)  {
    if (emergencymode == false)    {
      Firemode = 2;                                 // Burst
    }
    else {
      Firemode = 3;                                 // in emergency mode Burst fire will not work properly, but Full Auto will (more or less), you can change it to 1 for Semi
    }
  }
  else if (x > AutoLow & x < AutoHigh)  {
    Firemode = 3;                                 // Full Auto
  }
  else {
    Firemode = 1;                                 // Function will return "Semi" if the gear has to much clearence and is out of the set limits, or damaged.
  }
  return Firemode;
}

inline void cycle() {                     // subfunction cycle()
  if (emergencymode == false) {
    int startCycle = millis();

#ifdef DEBUG
    Serial.println("Starting to cycle");
    Serial.print("startCycle: ");
    Serial.println(startCycle);
#endif // DEBUG

    digitalWrite(PIN_FET, HIGH);

    do {
      colBouncer.update();
      // limiting the maximum cycle time
      if (millis() - startCycle > MAX_CYC) {
        digitalWrite(PIN_FET, LOW);

#ifdef DEBUG
        Serial.println("Cycling too long: ");
        Serial.print(millis() - startCycle);
        Serial.println(" milliseconds");
#endif // DEBUG
        errorCnt++;
        lasterror = millis();

        if (millis() - lasterror > 20000) {           // errorCnt will be set to 0, if the last error was 20s ago.
          errorCnt = 0;
        }

        if (errorCnt >= 3 ) {
          emergencymode = true;
#ifdef DEBUG
          Serial.print("Emergency mode activating");
#endif
        }
        break;

        /*     //setup(); commented out
          If it was just a one time error, you don´t have to configure the fire modes again, just hit the trigger again.
          If the airsoft still isn`t working either the COL push button is damaged or maybe a pin got loose.
          In both situations you have to disassemble the airsoft.
          Now implemented an emergency mode, so you can play to the end of the round, Burst will not work correctly then!
          Burst will be changed then to Full Auto, you can change that to Semi*/
      }
    } while (!colBouncer.rose());

    digitalWrite(PIN_FET, LOW);

    int endCycle = millis();
    cycleLength = endCycle - startCycle;
#ifdef DEBUG
    Serial.print("Cycle ends, took ");
    Serial.print(cycleLength);
    Serial.println(" milliseconds");
#endif // DEBUG
  }

  else {                                            // Emergency Mode
#ifdef DEBUG
    Serial.print("Emergency mode running");
#endif

    while (digitalRead(PIN_TRG) == LOW) {             //PIN_FET will be HIGH, as long you hold the trigger, a classic switch unit (CUT OFF) will disconnect the PIN_TRG!
      digitalWrite(PIN_FET, HIGH);
    } {
      digitalWrite(PIN_FET, LOW);
    }
  }
}



void isr_fire() {                                  //ISR = Interrupt Service Routine

#ifdef DEBUG
  Serial.println("Interrupt caught");
#endif // DEBUG

  int currTrigger = millis();
  // debouncing
  if (currTrigger - lastTrigger > DEB_TRG) {      //DEB_TRG is 50ms
    triggerPressed = true;
  }
  lastTrigger = currTrigger;
}

/*******************************************************************************
  Loop
*******************************************************************************/

void loop() {
  if (triggerPressed) {
    int Firemode = F_Firemode();                    //request of firemode subroutine
    detachInterrupt(PIN_TRG);

#ifdef DEBUG
    Serial.println("Trigger has been pressed");
#endif // DEBUG

    // SEMI MODE
    if (Firemode == 1) {
#ifdef DEBUG
      Serial.println("Semi mode");
#endif // DEBUG
      cycle();
    }

    // BURST MODE
#ifdef ENABLE_BURST
    else if (Firemode == 2) {
#ifdef DEBUG
      Serial.println("Burst mode");
#endif // DEBUG
      for (int i = 0; i < BURST_CNT; i++) {
        cycle();
        if (RPM_LIM > 0) {
          delay(rpmDelay - cycleLength);
        }
      }
    }
#endif

    // FULL AUTO MODE
#ifdef ENABLE_FULLAUTO
    else if (Firemode == 3) {
#ifdef DEBUG
      Serial.println("Full auto mode");
#endif // DEBUG
      while (digitalRead(PIN_TRG) == LOW) {
        cycle();
        if (RPM_LIM > 0) {
          delay(rpmDelay - cycleLength);
        }
      }
    }
#endif

    // reset trigger
    attachInterrupt(PIN_TRG, isr_fire, FALLING);          // ISR = Interrupt Service Routine
    triggerPressed = false;
  }

  if (analogRead(PIN_SEL) > SafeLow & analogRead(PIN_SEL) < SafeHigh)   //sleep while selecting Safe
  {
    LowPower.attachInterruptWakeup(PIN_TRG, isr_fire, FALLING);
#ifdef DEBUG
    Serial.print("Safe Mode selected, activating sleep mode");
#endif
    LowPower.sleep();
  }

  if (millis() - lastTrigger > 10000) {                     //sleep after 10s inactivity
    LowPower.attachInterruptWakeup(PIN_TRG, isr_fire, FALLING);
#ifdef DEBUG
    Serial.println("Last trigger 10s ago, activating sleep mode");
#endif
    LowPower.sleep();
  }
}
//using only sleep mode, because deepsleep mode will reset once after wake up and you must configure fire modes again...
