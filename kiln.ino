/* Electric kiln controller v1.0
 *   by Steve Turner (arduinokiln@gmail.com)
 *   See ReadMe.txt
 */

// Libraries to include
#include <LiquidCrystal.h>  // LCD display library
#include <max6675.h>        // Thermocouple card library
#include <PID_v1.h>         // PID temp control library
#include <SPI.h>            // Serial Peripheral Interface library
#include <SD.h>             // SD memory card library (SPI is required)
#include <Rotary.h>


const byte upFull[8] = {
  0b11111,
  0b11111,
  0b11111,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000
};

const byte lowFull[8] = {
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b11111,
  0b11111,
  0b11111
};

const byte empty[8] = {
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000
};

const byte full[8] = {
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111,
  0b11111
};


const int** numbersArray[10][12]  = {
  {1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1},
  {1, 1, 0, 0, 1, 0, 0, 1, 0, 1, 1, 1},
  {1, 1, 1, 2, 2, 1, 1, 3, 3, 1, 1, 1},
  {1, 1, 1, 2, 2, 1, 3, 3, 1, 1, 1, 1},
  {1, 0, 1, 1, 2, 1, 3, 3, 1, 0, 0, 1},
  {1, 1, 1, 1, 2, 2, 3, 3, 1, 1, 1, 1},
  {1, 3, 3, 1, 0, 0, 1, 3, 1, 1, 2, 1},
  {1, 1, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1},
  {1, 3, 1, 1, 2, 1, 1, 3, 1, 1, 2, 1},
  {1, 1, 1, 1, 0, 1, 3, 3, 1, 2, 2, 1}
};

bool buttonAction = false;


// Setup user variables (CHANGE THESE TO MATCH YOUR SETUP)
const int lcdRefresh = 500;           // Refresh rate to update screen when running (ms)
const int maxTemp = 1600;              // Maximum temperature (degrees).  If reached, will shut down.
const int pidCycle = 500;             // Time for a complete PID on/off cycle for the heating elements (ms)
double pidInput; //ACTUAL TEMP READING FROM THERMOCOUPLE.
double pidOutput; //RELAY FOR HEATER.
double pidSetPoint; // TEMP YOU ARE TRYING TO REACH.

PID pidCont = PID(&pidInput, &pidOutput, &pidSetPoint, 800, 47.37, 4.93, DIRECT);

const long saveCycle = 15000;          // How often to save current temp / setpoint (ms)

const int tempRange = 2;               // This is how close the temp reading needs to be to the set point to shift to the hold phase (degrees).  Set to zero or a positive integer.
const char tempScale = 'C';            // Temperature scale.  F = Fahrenheit.  C = Celsius

// Rotary encoder is wired with the common to ground and the two
// outputs to pins 2 and 3.
Rotary rotary = Rotary(2, 3);

//Rotary encoder's pushbutton is wired with the common to ground and the pin is set to inputPullup.
int rotaryPush = 4;

// thermocouple(SCK, CS, SO)
MAX6675 thermo = MAX6675(5, 6 ,7);  // Pins connected to the thermocouple card.  This is an array for each thermocouple (zone).
//MAX6675 thermo[numZones] = {MAX6675(5, 6 ,7)};  // Pins connected to the thermocouple card.  This is an array for each thermocouple (zone). OLD
const int heaterPin = 9;            // Pins connected to relays for heating elements.  This is an array for each output pin (zone).
                                                // Pins 10 thru 13 are for SD card.  These are automatically setup.
const int redundantHeater = 8;
bool scheduleStartup = false;
//rs, en, d4, d5, d6, d7
LiquidCrystal lcd(14, 15, 16, 17, 18, 19);      // LCD display (connected to analog inputs / reverse order so I don't have to twist ribbon cable)

// Setup other variables (DON'T CHANGE THESE)
double calcSetPoint;        // Calculated set point (degrees)
unsigned long holdStart;    // Exact time the hold phase of the segment started (ms).  Based on millis().
int i = 0;                  // Simple loop counter
int lastSeg = 0;            // Last segment number in firing schedule
int lastTemp;               // Last setpoint temperature (degrees)
unsigned long lcdStart;     // Exact time you refreshed the lcd screen (ms).  Based on millis().
int optionNum = 1;          // Option selected from screen #3
unsigned long pidStart;     // Exact time you started the new PID cycle (ms).  Based on millis().
double rampHours;           // Time it has spent in ramp (hours)
unsigned long rampStart;    // Exact time the ramp phase of the segment started (ms).  Based on millis().
unsigned long saveStart;    // Exact time you saved the temps (ms).  Based on millis().
File saveFile;              // File to save temps / set point.
char schedDesc1[21];        // Schedule description #1 (first line of text file)
int schedNum = 1;           // Current firing schedule number.  This ties to the file name (ex: 1.txt, 2.txt).
boolean schedOK = false;    // Is the schedule you loaded OK?
unsigned long schedStart;   // Exact time you started running the schedule (ms).  Based on millis().
int screenNum = 1;          // Screen number displayed during firing (1 = temps / 2 = schedule info / 3 = tools / 4 = done
int segHold[20];            // Hold time for each segment (min).  This starts after it reaches target temp.
int segNum = 0;             // Current segment number running in firing schedule.  0 means a schedule hasn't been selected yet.
boolean segPhase = 0;       // Current segment phase.  0 = ramp.  1 = hold.
int segRamp[20];            // Rate of temp change for each segment (deg/hr).
int segTemp[20];            // Target temp for each segment (degrees).

//******************************************************************************************************************************
//  SETUP: INITIAL SETUP (RUNS ONCE DURING START)
//******************************************************************************************************************************
void setup() {
  Serial.begin(9600);
  // Setup all pin modes on board.  Remove INPUT_PULLUP if you have resistors in your wiring.
  pinMode(4, INPUT_PULLUP);
  pinMode(heaterPin, OUTPUT);
  pinMode(redundantHeater, OUTPUT);
  digitalWrite(redundantHeater, LOW);
  digitalWrite(heaterPin, HIGH);
  for (i = 14; i <= 19; i++) {  // Change analog inputs to digital outputs for LCD screen
    pinMode(i, OUTPUT);
  }

  // Setup lcd display (20 columns x 4 rows)

  lcd.begin(20,4);
  lcd.createChar(0, empty);
  lcd.createChar(1, full);
  lcd.createChar(2, lowFull);
  lcd.createChar(3, upFull);

  // Setup SD card
  if (SD.begin() == false) {
    lcd.print(F("       ERROR:"));
    lcd.setCursor(0, 2);
    lcd.print(F("Can't setup SD card"));
    lcd.setCursor(0, 3);
    lcd.print(F("System was shut down"));
    shutDown();
  }

  lcd.setCursor(0,0);
  lcd.print(F("YOU ARE ABOUT TO"));
  lcd.setCursor(0,1);
  lcd.print(F("DELETE YOUR LOGS"));
  lcd.setCursor(0,2);
  lcd.print(F("PRESS BTN TO ACCEPT"));
  // Delete old save file
  while(1){
    if (digitalRead(4) == LOW){
      break;
    }
  }
  lcd.clear();
  lcd.print("deleting...");
  SD.remove("temps.txt");
  delay(1000);

  // Open firing shedule # 1
  openSched();

}

//******************************************************************************************************************************
//  LOOP: MAIN LOOP (CONTINUOUS)
//******************************************************************************************************************************
void loop() {

  //******************************
  // Shutdown if too hot
  // FIX
  //CHANGE THIS FOR A TEMP RUNOFF WATCHDOG
  if (pidInput >= maxTemp) {
    lcd.clear();
    lcd.print(F("       ERROR:"));
    lcd.setCursor(2, 2);
    lcd.print(F("Max temp reached"));
    lcd.setCursor(0, 3);
    lcd.print(F("System was shut down"));
    shutDown();
  }
  unsigned char result = rotary.process();
  button();
  //******************************
  // Select a firing schedule
  if (segNum == 0) {

    // Up arrow button
    if (result == DIR_CCW && schedNum > 1) {
      lcd.clear();
      schedNum = schedNum - 1;
      openSched();
    }

    // Down arrow button
    if (result == DIR_CW) {
      lcd.clear();
      schedNum = schedNum + 1;
      openSched();
    }

    // Select / Start button
    if (buttonAction == true && schedOK == true) {
      lcd.clear();
      buttonAction = false;
      setupPID();
      segNum = 1;
      unsigned long mil = millis();
      lcdStart = mil;
      pidStart = mil;
      rampStart = mil;
      schedStart = mil;
      updateLCD();
    }
  }

  //******************************
  // Running the firing schedule
  if (segNum >= 1) {
    if (scheduleStartup == false){
      digitalWrite(8, HIGH);
      scheduleStartup = true;
    }

    // Up arrow button
    if (result == DIR_CCW) {
      lcd.clear();
      if (screenNum == 2 || (screenNum == 3 && optionNum == 1)) {
        screenNum = screenNum - 1;
      }
      else if (screenNum == 3 && optionNum >= 2) {
        optionNum = optionNum - 1;
      }
      updateLCD();
    }

    // Down arrow button
    if (result == DIR_CW) {
      lcd.clear();
      if (screenNum <= 2) {
        screenNum = screenNum + 1;
      }
      else if (screenNum == 3 && optionNum <= 2) {
        optionNum = optionNum + 1;
      }
      updateLCD();
    }

    // Select / Start button
    if (buttonAction == true && screenNum == 3) {
      lcd.clear();
      buttonAction = false;
      if (optionNum == 1) {  // Add 5 min
        segHold[segNum - 1] = segHold[segNum - 1] + 5;
        optionNum = 1;
        screenNum = 2;
      }

      if (optionNum == 2) {  // Add 5 deg
        segTemp[segNum - 1] = segTemp[segNum - 1] + 5;
        optionNum = 1;
        screenNum = 1;
      }

      if (optionNum == 3) {  // Goto next segment
        segNum = segNum + 1;
        optionNum = 1;
        screenNum = 2;
      }

      updateLCD();
    }

    // Update PID's / turn on heaters / update segment info
    if (screenNum < 4) {
      if (millis() - pidStart >= pidCycle) {
        pidStart = millis();
        updatePIDs();
      }
      htrControl();
      updateSeg();
    }

    // Refresh the LCD
    if (millis() - lcdStart >= lcdRefresh) {
      updateLCD();
      lcdStart = millis();
    }
    if (millis() - saveStart >= saveCycle && pidInput > 0 && screenNum < 4) {
      saveSd();
    }
  }

}


void bigNumbers(char number, int Offset) {

  int column = 0;
  for (int row = 0; row < 4; row++) {
    //column, row
    lcd.setCursor(column + Offset, row);
    for (int x = 0; x < 3; x++) {
      int character = int(numbersArray[number - '0'][x + (row * 3)]);
      lcd.write(byte(character));
      //delay(2000);
    }
  }
}


int prevLen = 0;

void printBigBuffer(char *numbers){
  if (prevLen != strlen(numbers)){
    lcd.clear();
  }
  prevLen = strlen(numbers);
  for (int i = 0; i < strlen(numbers); i++)
  {
    bigNumbers(numbers[i], (i * 3) + i);
  }
  lcd.setCursor(strlen(numbers) * 4, 0);
  lcd.print("o");
}


void saveSd(){
  // Save the temps to a file on SD card
  saveFile = SD.open("temps.txt", FILE_WRITE);
    saveFile.print((millis() - schedStart) / 60000.0); // Save in minutes
    saveFile.print(",");
    saveFile.print(pidInput);
    saveFile.print(",");
    saveFile.println(pidSetPoint);
  saveFile.close();

  saveStart = millis();
}

// Variables will change:
int buttonState;             // the current reading from the input pin
int lastButtonState = HIGH;   // the previous reading from the input pin

// the following variables are unsigned longs because the time, measured in
// milliseconds, will quickly become a bigger number than can be stored in an int.
unsigned long lastDebounceTime = 0;  // the last time the output pin was toggled
unsigned long debounceDelay = 50;    // the debounce time; increase if the output flickers

void button(){
  int reading = digitalRead(4);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        buttonAction = true;
      }
    }
  }
  lastButtonState = reading;
}


//******************************************************************************************************************************
//  BTNBOUNCE: HOLD UNTIL BUTTON IS RELEASED.  DELAY FOR ANY BOUNCE
//******************************************************************************************************************************
void btnBounce(int btnPin) {

  while (digitalRead(btnPin) == HIGH);
  delay(40);

}

//******************************************************************************************************************************
//  HTRCONTROL: TURN HEATERS ON OR OFF
//******************************************************************************************************************************
void htrControl() {

  if (pidOutput >= millis() - pidStart) {
    digitalWrite(heaterPin, LOW);
  }
  else {
    digitalWrite(heaterPin, HIGH);
  }

}

//******************************************************************************************************************************
//  INTLENGTH: GET THE LENGTH OF A INTEGER
//******************************************************************************************************************************
int intLength(int myInt) {

  myInt = abs(myInt);

  if (myInt >= 10000) {
    return 5;
  }
  else if (myInt >= 1000) {
    return 4;
  }
  else if (myInt >= 100) {
    return 3;
  }
  else if (myInt >= 10) {
    return 2;
  }
  else {
    return 1;
  }

}

//******************************************************************************************************************************
//  OPENSCHED: OPEN AND LOAD A FIRING SCHEDULE FILE / DISPLAY ON SCREEN
//******************************************************************************************************************************
void openSched() {

  // Setup all variables
  int col = 1;          // Column number (of text file).  First column is one.
  int row = 1;          // Row number (of text file).  First row is one.
  char tempChar;        // Temporary character holder (read one at a time from file)
  char tempLine[21];    // Temporary character array holder
  int tempLoc = 0;      // Current location of next character to place in tempLine array
  char schedDesc2[21];  // Schedule description #2 (second line of text file)
  char schedDesc3[21];  // Schedule description #3 (third line of text file)

  // Clear the arrays
  memset(schedDesc1, 0, sizeof(schedDesc1));
  memset(segRamp, 0, sizeof(segRamp));
  memset(segTemp, 0, sizeof(segTemp));
  memset(segHold, 0, sizeof(segHold));

  // Make sure you can open the file
  sprintf(tempLine, "%d.txt", schedNum);
  File myFile = SD.open(tempLine, FILE_READ);

  if (myFile == false) {
    lcd.clear();
    lcd.print(F("SELECT SCHEDULE: "));
    lcd.print(schedNum);
    lcd.setCursor(0, 2);
    lcd.print(F("Can't find/open file"));
    schedOK = false;
    return;
  }

   // Load the data
  while (myFile.available() > 0) {

    // Read a single character
    tempChar = myFile.read();

    if (tempChar == 13) {       // Carriage return: Read another char (it is always a line feed / 10).  Add null to end.
      myFile.read();
      tempLine[tempLoc] = '\0';
    }
    else if (tempChar == 44) {  // Comma: Add null to end.
      tempLine[tempLoc] = '\0';
    }
    else if (tempLoc <= 19) {   // Add it to the temp line array
      tempLine[tempLoc] = tempChar;
      tempLoc = tempLoc + 1;
    }

    if (row == 1 && tempChar == 13) {
      memcpy(schedDesc1, tempLine, 21);
    }
    else if (row == 2 && tempChar == 13) {
      memcpy(schedDesc2, tempLine, 21);
    }
    else if (row == 3 && tempChar == 13) {
      memcpy(schedDesc3, tempLine, 21);
    }
    else if (row >= 4 && col == 1 && tempChar == 44) {
      segRamp[row - 4] = atoi(tempLine);
    }
    else if (row >= 4 && col == 2 && tempChar == 44) {
      segTemp[row - 4] = atoi(tempLine);
    }
    else if ((row >= 4 && col == 3 && tempChar == 13) || myFile.available() == 0) {
      segHold[row - 4] = atoi(tempLine);
    }

    if (tempChar == 13) {  // End of line.  Reset everything and goto next line
      memset(tempLine, 0, 21);
      tempLoc = 0;
      row = row + 1;
      col = 1;
    }

    if (tempChar == 44) {  // Comma.  Reset everything and goto 1st column
      memset(tempLine, 0, 21);
      tempLoc = 0;
      col = col + 1;
    }

  }  // end of while(myFile.available ...

  // Close the file
  myFile.close();

  // Set some variables
  lastSeg = row - 3;
  schedOK = true;

  // Fix Ramp values so it will show the correct sign (+/-).  This will help to determine when to start hold.
  for (i = 0; i < lastSeg; i++) {
    segRamp[i] = abs(segRamp[i]);
    if (i >= 1) {
      if (segTemp[i] < segTemp[i - 1]) {
        segRamp[i] = -segRamp[i];
      }
    }
  }

  // Display on the screen
  lcd.clear();
  lcd.print(F("SELECT SCHEDULE: "));
  lcd.print(schedNum);
  lcd.setCursor(0, 1);
  lcd.print(schedDesc1);
  lcd.setCursor(0, 2);
  lcd.print(schedDesc2);
  lcd.setCursor(0, 3);
  lcd.print(schedDesc3);

  // Cut down schedule description so it shows better on other screen when running
  schedDesc1[14 - intLength(schedNum)] = '\0';

}

//******************************************************************************************************************************
//  READTEMPS: Read the temperatures
//******************************************************************************************************************************
void readTemps() {

  pidInput = thermo.readCelsius();

}

//******************************************************************************************************************************
//  SETUPPIDS: INITIALIZE THE PID LOOP
//******************************************************************************************************************************
void setupPID() {

  pidCont.SetSampleTime(pidCycle);
  pidCont.SetOutputLimits(0, pidCycle);
  pidCont.SetMode(AUTOMATIC);

}

//******************************************************************************************************************************
//  SHUTDOWN: SHUT DOWN SYSTEM
//******************************************************************************************************************************
void shutDown() {

  // Turn off all zones (heating element relays)
    digitalWrite(heaterPin, HIGH);
    digitalWrite(redundantHeater, LOW);

  // Disable interrupts / Infinite loop
  cli();
  while (1);

}

//******************************************************************************************************************************
//  UPDATELCD: UPDATE THE LCD SCREEN WHEN RUNNING
//******************************************************************************************************************************
void updateLCD() {

  // Clear screen and set cursor to top left
//  lcd.clear();

  // Temperatures
  if (screenNum == 1) {
    char numChar[intLength(pidInput) + 1];
    itoa(pidInput, numChar, 10);
    printBigBuffer(numChar);
    lcd.setCursor(16, 2);
    lcd.write("goal");
    lcd.setCursor(16, 3);
    lcd.print(int(pidSetPoint));
  }

  // Schedule / segment info
  if (screenNum == 2) {
    lcd.setCursor(0, 0);
    lcd.print(F("SCH "));
    lcd.print(schedNum);
    lcd.print(F(": "));
    lcd.print(schedDesc1);

    lcd.setCursor(0, 1);
    lcd.print(F("SEG: "));
    lcd.print(segNum);
    lcd.print(F(" / "));
    lcd.print(lastSeg);

    if (segPhase == 0) {
      lcd.setCursor(2, 2);
      lcd.print(F("Ramp to "));
      lcd.print(segTemp[segNum - 1]);
      lcd.print(F(" "));
      lcd.print((char)223);
      lcd.print(tempScale);

      lcd.setCursor(2, 3);
      lcd.print(F("at "));
      lcd.print(segRamp[segNum - 1]);
      lcd.print(F(" "));
      lcd.print((char)223);
      lcd.print(F("/hr"));
    }
    else {
      lcd.setCursor(2, 2);
      lcd.print(F("Hold at "));
      lcd.print(segTemp[segNum - 1]);
      lcd.print(F(" "));
      lcd.print((char)223);
      lcd.print(tempScale);

      lcd.setCursor(2, 3);
      lcd.print(F("for "));
      lcd.print((millis() - holdStart) / 60000);
      lcd.print(F(" / "));
      lcd.print(segHold[segNum - 1]);
      lcd.print(F(" min"));
    }
  }

  // Tools
  if (screenNum == 3) {
    lcd.setCursor(0, 0);
    lcd.print(F("       TOOLS:"));
    lcd.setCursor(2, 1);
    lcd.print(F("Add 5 min"));
    lcd.setCursor(2, 2);
    lcd.print(F("Increase 5 deg"));
    lcd.setCursor(2, 3);
    lcd.print(F("Skip to next seg"));
    lcd.setCursor(0, optionNum);
    lcd.print(F(">"));
    lcd.setCursor(19, optionNum);
    lcd.print(F("<"));
  }

  // Schedule completed
  if (screenNum == 4) {
    readTemps();

    lcd.setCursor(0, 0);
    lcd.print(F(" SCHEDULE COMPLETE"));
    lcd.setCursor(2, 1);
    lcd.print(F("Wait until cool"));
    lcd.setCursor(2, 2);
    lcd.print(F("before you open"));
    lcd.setCursor(2, 3);
    lcd.print(F("Zone 1: "));
    lcd.print((int)pidInput);
    lcd.print(F(" "));
    lcd.print((char)223);
    lcd.print(tempScale);
  }

}

//******************************************************************************************************************************
//  UPDATEPIDS: UPDATE THE PID LOOPS
//******************************************************************************************************************************
void updatePIDs() {

  // Get the last target temperature
  if (segNum == 1) {  // Set to room temperature for first segment
    if (tempScale == 'C') {
      lastTemp = 24;
    }
    if (tempScale == 'F') {
      lastTemp = 75;
    }
  }
  else {
    lastTemp = segTemp[segNum - 2];
  }

  // Calculate the new setpoint value.  Don't set above / below target temp
  if (segPhase == 0) {
    rampHours = (millis() - rampStart) / 3600000.0;
    calcSetPoint = lastTemp + (segRamp[segNum - 1] * rampHours);  // Ramp
    if (segRamp[segNum - 1] >= 0 && calcSetPoint >= segTemp[segNum - 1]) {
      calcSetPoint = segTemp[segNum - 1];
    }
    if (segRamp[segNum - 1] < 0 && calcSetPoint <= segTemp[segNum - 1]) {
      calcSetPoint = segTemp[segNum - 1];
    }
  }
  else {
    calcSetPoint = segTemp[segNum - 1];  // Hold
  }

  // Read the temperatures
  readTemps();

  // Set the target temp.  Add any offset.
  pidSetPoint = calcSetPoint;

  // Update the PID based on new variables
  pidCont.Compute();

}

//******************************************************************************************************************************
//  UPDATESEG: UPDATE THE PHASE AND SEGMENT
//******************************************************************************************************************************
void updateSeg() {

  // Start the hold phase
  if ((segPhase == 0 && segRamp[segNum - 1] < 0 && pidInput <= (segTemp[segNum - 1] + tempRange)) ||
      (segPhase == 0 && segRamp[segNum - 1] >= 0 && pidInput >= (segTemp[segNum - 1] - tempRange))) {
    segPhase = 1;
    holdStart = millis();
  }

  // Go to the next segment
  if (segPhase == 1 && millis() - holdStart >= segHold[segNum - 1] * 60000) {
    segNum = segNum + 1;
    segPhase = 0;
    rampStart = millis();
  }

  // Check if complete / turn off all zones
  if (segNum - 1 > lastSeg) {
    digitalWrite(heaterPin, HIGH);
    digitalWrite(redundantHeater, LOW);
    screenNum = 4;
  }

}
