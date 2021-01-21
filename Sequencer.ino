// ---------------------------------------------------------------------------------------------------------------------------
// Code written by David Wieland aka Datulab Tech. Feel free to use this in your own projects and improve upon it. If you make
// something cool, please let me know. This code has been written specifically for the Arduino nano and RGBW Neopixel LEDs. 
// If you want to use a differetn configuration, make sure to update the code. 
// C++ is not my main programming language, so please excuse if the code could be a bit nicer in some areas. 
// If you have any questions, make sure to check out the video on my YouTube channel about this.
// If there still is anything, write me a message to info@datulab.tech
// ---------------------------------------------------------------------------------------------------------------------------

#include <Adafruit_NeoPixel.h>
#include <EEPROM.h>

//Initializing the LEDs
Adafruit_NeoPixel strip = Adafruit_NeoPixel(16, 13, NEO_GRBW + NEO_KHZ800);

//Settings
#define whiteBrightness 15
#define voicePin A2
#define bpmPin A1
#define patternPin A0
#define syncPin A7
#define numPatterns 5                           //number of patterns including one random
#define patternChangeDuration 1000              //how long pattern change is displayed
#define minBpm 20                               //sets minimum bpm
#define bpmScale 3                              //ratio bpm above min is scaled with, at 3 bpm scales from 20 - 360
#define trigDuration 10                         //time in ms that trigger is high

//Switch matrix variables
byte inputs[] = {6, 7, 8, 9, 10};
const byte inCount = sizeof(inputs) / sizeof(inputs[0]);
byte outputs[] = {2, 3, 4, 5};
const byte outCount = sizeof(outputs) / sizeof(outputs[0]);

const byte outputPins[] = {17, 18, 19, 20};

//State variables
bool lengthSelect;
bool trig;
bool sync;
int bpm = 120;
byte voice = 0;
byte pattern = 0;
bool beat[4 * numPatterns][16];                 //[4 voices numPatterns (minus random pattern) patterns][16 beats] true for hit false for no hit
byte lengths[4 * (numPatterns - 1)];            //length 1-15 of the voices
byte gateLength = 14;                           //length of the gate output relative to the beat length (0-15)

//Temporary variables
byte pos;
byte saveSlot;
bool stateChanged;
uint32_t currentColor; 
bool isActive[16];
int patternPot = 1025;
int previousPatternPot;
int voicePot;
int bpmPot;
bool gateChange;
bool patternChange;
bool syncHigh;

//Timing variables
unsigned long currentTime;                      //saves current time
unsigned long previousBeat;                     //time of last beat change
unsigned long previousPatternTime;                  //time of patternChange engage
long beatDuration;
long gateDuration;
byte currentBeat[numPatterns];

//Colors
byte voiceColor[4][4] = {{30, 10, 0, 0},
                         {50, 0, 2, 0},
                         {5, 0, 30, 0},
                         {0, 25, 10, 0}};


// ---------------------------------------------------------------------------------------------------------------------------
// -------------------------------------------- Initial Setup ----------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------
void setup() {
  
  for(int i=0; i<outCount; i++){                //Initializes all the switch matrix pins
    pinMode(outputs[i],OUTPUT);
    digitalWrite(outputs[i],HIGH);
  }
  for(int i=0; i<inCount; i++){
    pinMode(inputs[i],INPUT_PULLUP);
  }

  pinMode(syncPin, INPUT);
  pinMode(voicePin, INPUT);
  pinMode(bpmPin, INPUT);
  pinMode(patternPin, INPUT);
  
  Serial.begin(9600);                           //Establishes connection to computer for debuging
  Serial.println("Connected");

  strip.begin();                                //Initializes the LEDs and displays light show
  for(int i = 0; i < 16; i++){
    strip.setPixelColor(i, i * 4, 40 - 2 * i, 20 - i, 0);
    strip.show();
    delay(50);
  }
                                                //reading variables from EEPROM
  int value = 0;                                //overcomplicated way of finding the next save location
  int previousValue = 0;
  int startAddress = 64;
  
  for(int i = 0; i < 16; i++){                  //finds newest starting address by comparing first byte of all locations
    value = EEPROM.read(i * 64);
    if(value == previousValue + 1 || previousValue == 255 || (i == 0 && value != 0)){
      previousValue = value;
      startAddress = i * 64;
      
    }
  }

  gateLength = EEPROM.read(startAddress + 1);   //reads the gate length

  for(int pat = 0; pat < 4; pat++){             //the 2 nested for loops iterate through the patterns and voices
    for(int voi = 0; voi < 4; voi++){
      
      int voiceStart = startAddress + 2 + 3 * (voi + 4 * pat); //starting address of voice
      
      int beat1 = EEPROM.read(voiceStart);      //the 2 integers are converted back to 16 bool values
      for(int i = 0; i < 8; i++){
        if(beat1 / (1 << (7 - i)) > 0){
          beat[voi + 4 * pat][7 - i] = true;
          beat1 = beat1 - (1 << (7 - i));
        }else{
          beat[voi + 4 * pat][7 - i] = false;
        }
      }

      int beat2 = EEPROM.read(voiceStart + 1);
      for(int i = 0; i < 8; i++){
        if(beat2 / (1 << (7 - i)) > 0){
          beat[voi + 4 * pat][15 - i] = true;
          beat2 = beat2 - (1 << (7 - i));
        }else{
          beat[voi + 4 * pat][15 - i] = false;
        }
      }
      
      lengths[voi + 4 * pat] = EEPROM.read(voiceStart + 2); //reading the length value
    }
  }
}


// ---------------------------------------------------------------------------------------------------------------------------
// -------------------------------------------- Reading the switches ---------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------


void readSwitches(){                              //Reading the switches and updating the beat, lengths, lengthSelect, gate, and sync variables with the current switch states
  saveSlot = voice + 4 * pattern;
  
  for (int i = 0; i < 4; i++){                    //iterates through the 4 outputs
    digitalWrite(outputs[i],LOW);
    delayMicroseconds(5);
      
    for(int j = 0; j < 5; j++){                   //iterates throught the 5 inputs
      bool state;                                 //state of current switch
      if(digitalRead(inputs[j])){
        state = false;
      }else{
        state = true;
      }
      pos = 4 * j + i;                            //current switch number
      
      if(j < 4 && voice != 4){                    //if on one of the beat switches
        if(lengthSelect && state){                //if in length selecting mode
          lengths[saveSlot] = pos;
          stateChanged = true;
        }                                         //otherwise if switch is newly engaged
        else if(state && !isActive[pos]){
          beat[saveSlot][pos] = !beat[saveSlot][pos];
          isActive[pos] = true;
          stateChanged = true;
        }                                         //resetting isActive if switch newly disengaged
        else if(!state && isActive[pos]){
          isActive[pos] = false;
        }
      }
      
      else if (j == 4){                           //if on one of the settings switches
        switch(i){
          case 0:
            lengthSelect = state;
          case 1:
            trig = state;
          case 2:
            sync = state;
        }
      }
    }
    digitalWrite(outputs[i],HIGH);
    delayMicroseconds(5);
  }
}


// ---------------------------------------------------------------------------------------------------------------------------
// -------------------------------------------- Reading the Pots -------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------

void readPots(){                                  //Reads pots and updates variables bpm, voice, pattern, and gate length with the current pot positions
  patternPot = analogRead(patternPin);            //reads pots
  bpmPot = analogRead(bpmPin);
  voicePot = analogRead(voicePin);
  
  if(patternPot < previousPatternPot - 5 || patternPot > previousPatternPot + 5){  //if pattern pot has been changed
    if(previousPatternPot != 1025){
      patternChange = true;                       //patternChange will switch LEDs to display pattern or gate length
    }
    previousPatternTime = millis();
    if(lengthSelect){                             //if in lenghth selecting mode
      gateLength = patternPot / 48;               //sets gateLength to a 0-15 number according to pattern pot
      gateChange = true;
    }
    else{                                         //if in normal operating mode
      pattern = patternPot * numPatterns / 1024;  //updates the pattern with pos of pattern pot
    }
    previousPatternPot = patternPot;
  }

  beatDuration = 60000 / (bpm * 4);               //updates beatDuration with current bpm value
  gateDuration = beatDuration * gateLength / 16;  //updates gateDuration with current beatDuration and gateLength
  bpm = minBpm + bpmPot / bpmScale;
  voice = voicePot * 5 / 1024;
}


// ---------------------------------------------------------------------------------------------------------------------------
// -------------------------------------------- Updating the LEDs ------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------

void displayLEDs(){
  if(lengthSelect && voice != 4){                 //if length switch is active
    if(patternChange){                            //gate length selection mode, represents relative gate length with red leds
      for(int i = 0; i < 16; i++){
        if(i <= gateLength){
          strip.setPixelColor(i, 50, 0, 0, 0);
        }else{
          strip.setPixelColor(i, 0, 0, 0, 0);
        }
      }
    }
    else if(voice != 4){                          //length selection mode, turns all active beats white and others off
      for(int i = 0; i < 16; i++){
        if(i <= lengths[saveSlot]){
          strip.setPixelColor(i, 0, 0, 0, 15);
        }else{
          strip.setPixelColor(i, 0, 0, 0, 0);
        }
      }
    }
  }
  
  else if(patternChange){                         //if pattern has been changed recently, pattern nr is displayed with led
    for(int i = 0; i < 16; i++){
        if(i == pattern){
          strip.setPixelColor(i, 0, 0, 0, 15);
        }else{
          strip.setPixelColor(i, 0, 0, 0, 0);
        }
      }
  }
  
  else{                                           //normal operating mode, shows active beats in color of voice, turns others off
    if(voice != 4){                               //if in voice editing mode
      for(int i = 0; i < 16; i++){                
        if(beat[saveSlot][i]){                    //displays active beats for current voice
          strip.setPixelColor(i, voiceColor[voice][0], voiceColor[voice][1], voiceColor[voice][2], voiceColor[voice][3]);
        }
        else{
          strip.setPixelColor(i, 0, 0, 0, 0);
        }
      }
    }
    else{                                        //if in performance mode, all voices are displayed with voice 1 as highest priority
      for(int i = 0; i < 16; i++){
        strip.setPixelColor(i, 0, 0, 0, 0);
      }
      for(int j = 0; j < 4; j++){
        for(int i = 0; i < 16; i++){                  
          if(beat[3 - j + 4 * pattern][i]){
            strip.setPixelColor(i, voiceColor[3 - j][0], voiceColor[3 - j][1], voiceColor[3 - j][2], voiceColor[3 - j][3]);
          }
        }
      }
    }
    
    for(int i = 0; i < 16; i++){                  //highlights current beat
      if(currentBeat[voice] == i){
        strip.setPixelColor(i, 0, 0, 0, 25);
      }
    }
  }
  strip.show();
}


// ---------------------------------------------------------------------------------------------------------------------------
// -------------------------------------------- Generates random beat --------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------

void randomBeat(){                                //creates a random beat for all 4 voices with voice 1 the most beats and voice 4 the fewest
  for(int i = 0; i < 4; i++){
    for(int j = 0; j < 16; j++){
      if(random(10) > 4 + i){
        beat[16 + i][j] = true;
      }else{
        beat[16 + i][j] = false;
      }
    }
  }
}


// ---------------------------------------------------------------------------------------------------------------------------
// -------------------------------------------- Updates the beat -------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------

void advanceBeat(){                               //updates currentBeat, if it reached end for a voice it rolls over to first beat
  if(pattern != 4){
    for(int i = 0; i < 5; i++){
      if(i < 4){
        if(currentBeat[i] < lengths[i + 4 * pattern]){
          currentBeat[i]++;
        }else{
          currentBeat[i] = 0;
        }
      }else{
        if(currentBeat[i] < lengths[0 + 4 * pattern]){
          currentBeat[i]++;
        }else{
          currentBeat[i] = 0;
        }
      }
    }
  }
  else{                                           //if on random pattern, new pattern is generated when beat rolls over to first position
    if(currentBeat[0] < 15){
      currentBeat[0]++;
    }else{
      currentBeat[0] = 0;
      randomBeat();
    }
    for(int i = 0; i < 4; i++){
      currentBeat[i + 1] = currentBeat[0];
    }
  }
}


// ---------------------------------------------------------------------------------------------------------------------------
// -------------------------------------------- Updates the outputs ----------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------

void output(){
  if(!trig && previousBeat + gateDuration > currentTime){           //if in gate mode and during active gate time
    for(int i = 0; i < 4; i++){
      digitalWrite(outputPins[i],beat[saveSlot][currentBeat[i]]);
    }
  }
  else if(trig & previousBeat + trigDuration > currentTime){        //if in trig mode and during acrive trig time
    for(int i = 0; i < 4; i++){
      digitalWrite(outputPins[i],beat[saveSlot][currentBeat[i]]);
    }
  }
  else{                                                             //if not in active time, outputs are low
    for(int i = 0; i < 4; i++){
      digitalWrite(outputPins[i],LOW);
    }
  }
}


// ---------------------------------------------------------------------------------------------------------------------------
// -------------------------------------------- Checks time ------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------

void checkTiming(){                                                 //checks if enough time has passed to advance to the next beat
  
  currentTime = millis();                                           //gets current time in ms
  
  if(currentTime - previousBeat >= beatDuration){                   //if it is time for new beat
    previousBeat = currentTime;
    if(!sync){
      advanceBeat();
    }
  }
  
  if(patternChange && currentTime - previousPatternTime >= patternChangeDuration){ //if enough time has passed since pattern pot was last moved, returning to normal LED display
    patternChange = false;
    
    if(gateChange){                                                 //if gate length has changed, causes EEPROM write
      stateChanged = true;
      gateChange = false;
    }
  }
}


// ---------------------------------------------------------------------------------------------------------------------------
// -------------------------------------------- Checks Sync Input ------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------

void checkSync(){                                                   //advances the beat on every rising flank of the sync input
  if(analogRead(syncPin) > 600){
    if(!syncHigh){
      syncHigh = true;
      advanceBeat();
    }
  }
  else{
    syncHigh = false;
  }
}


// ---------------------------------------------------------------------------------------------------------------------------
// -------------------------------------------- Writes to EEPROM -------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------

//Writes current beat patterns to EEPROM
//beat patterns are converted to 2byte per voice
//order is 1byte gate length, then repeated for each voice of each pattern 2byte pattern, 1byte length
//as writes are rated to 100'000, location is moved by increment of 64byte every time to use EEPROM uniformally
//to find newest entry, first byte is incremented (rolls over at 255), so looking for highest number will get newest

void writeEEPROM(){
  stateChanged = false;
  
  int value = 0;                                                    //overcomplicated way of finding the next save location
  int previousValue = 0;
  int startAddress = 64;
  
  for(int i = 0; i < 16; i++){                                      //finds newest starting address by comparing first byte of all locations
    value = EEPROM.read(i*64);
    if(value == previousValue + 1 || previousValue == 255 || (i == 0 && value != 0)){
      previousValue = value;
      if(i < 15){
        startAddress = (i + 1) * 64;
      }else{
        startAddress = 0;
      }
    }
  }

  int index = 0;
  if(previousValue != 255){
    index = previousValue + 1;
  }  
  
  EEPROM.update(startAddress,index);                                //writes counter to first byte
  EEPROM.update(startAddress+1,gateLength);                         //writes gateLength to second byte
  
  for(int pat = 0; pat < 4; pat++){                                 //the 2 nested for loops iterate through the patterns and voices
    for(int voi = 0; voi < 4; voi++){
      int beat1 = 0;                                                //the 16 bool values for the beats get converted to 2 integers
      for(int i = 0; i < 8; i++){
        if(beat[voi + 4 * pat][i]){
          beat1 = beat1 + (1 << i);
        }
      }
      int beat2 = 0;
      for(int i = 0; i < 8; i++){
        if(beat[voi + 4 * pat][8+i]){
          beat2 = beat2 + (1 << i);
        }
      }

      int voiceStart = startAddress + 2 + 3 * (voi + 4 * pat);      //writing the two integers of the beats and the length to EEPROM
      EEPROM.update(voiceStart, beat1);
      EEPROM.update(voiceStart + 1, beat2);
      EEPROM.update(voiceStart + 2, lengths[voi + 4 * pat]);
    }
  }
}


// ---------------------------------------------------------------------------------------------------------------------------
// -------------------------------------------- Main Loop --------------------------------------------------------------------
// ---------------------------------------------------------------------------------------------------------------------------

void loop() {
  readPots();
  readSwitches();
  if(stateChanged){
    writeEEPROM();
  }
  displayLEDs();
  checkTiming();
  if(sync){
    checkSync();
  }
  output();
}
