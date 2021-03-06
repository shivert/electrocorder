#include <Adafruit_MPR121.h>
#include <Wire.h>

const int MIDI_CHANNEL = 1;                //MIDI channel number to send messages
const int FLOW_SENSOR = 2;                 //Pin location of the flow sensor
const int JOYSTICK_BUTTON = 6;             //Pin location of joystick button
const int PIN_LED_INDICATOR = 13;          //Pin location for the LED indicator
const int PIN_ANALOG_X = 20;               //Pin location for the horizontal motion of the joystick
const int PIN_ANALOG_Y = 21;               //Pin location for the vertical motion of the joystick

volatile int NbTopsFan;                    //measuring the rising edges of the signal of the flow sensor
int flow = 0, lastFlow = 0;                //variables used to store flow information

Adafruit_MPR121 cap = Adafruit_MPR121();   //initialize the board for the cap touch
uint16_t lastTouched = 0;                  //last touched fingering combination
uint16_t currTouched = 0;                  //current fingering combination

// Note Mappings
// NOTE_MIDDLE_C   --->    1
// NOTE_D          --->    2
// NOTE_E          --->    3
// NOTE_F          --->    4
// NOTE_G          --->    5
// NOTE_A          --->    6
// NOTE_B          --->    7
// NOTE_C          --->    8
// NOTE_D_2        --->    9
// NOTE_B_FLAT     --->    10

const int numberOfNotes = 128; //total Number of MIDI notes
const byte noteButtons[] = {255, 127, 63, 31, 15, 7, 3, 5, 4, 27}; //integer values corresponding to button confiugration
const int noteMIDI[] = {60, 62, 64, 65, 67, 69, 71, 72, 74, 70}; //corresponding MIDI notes
bool notesEnabled[127] = {false}; //array to keep track of which notes are currently on

bool killAll = false;
bool vibratoEnabled = false;
bool validNote = false;

// Joystick Direction Mapping
// 0 --> normal
// 1 --> down
// 2 --> up
// 3 --> left
// 4 --> right

int joystick_x_position, joystick_y_position, joystick_buttonState;
int lastJoystickPosition = 0;
int currJoystickPosition = 0;
int currOctave = 0;

double vibratoValue = 0.0;
float noteStartTime = 0;

void setup()
{
  Serial.begin(9600);
  Serial.println("Group 5 Electrocorder");

  pinMode(FLOW_SENSOR, INPUT_PULLUP);
  pinMode(JOYSTICK_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED_INDICATOR, OUTPUT);
  attachInterrupt(FLOW_SENSOR, incrementCount, RISING);

  // Default address is 0x5A, if tied to 3.3V its 0x5B
  // If tied to SDA its 0x5C and if SCL then 0x5D
  if (!cap.begin(0x5A))
  {
    Serial.println("MPR121 not found, check wiring?");
    while (1);
  }
  Serial.println("MPR121 found!");
}

void loop()
{
  readInputs();
  checkForDoubleTonguing();
  findJoystickDirection();
  checkForValidCombo(currTouched);

  //Set NbTops to 0 ready for flowulations
  NbTopsFan = 0;

  //Enables interrupts
  sei();

  int tempJoystickPosition = didJoystickChange();
  if (tempJoystickPosition == 1 || tempJoystickPosition == 2 )
  {
    changeOctave(tempJoystickPosition);
  }

  changeVibrato(tempJoystickPosition);

  if (flow > 0 && !killAll)
  {
    if (shouldPrint(flow, lastFlow)) {
      Serial.println("Blowing");
    }
    playNote(currTouched);
  }
  else if (killAll) {
    turnOffAllNotes();
  }
  else
  {
    if (shouldPrint(flow, lastFlow)) {
      Serial.println("Not Blowing");
      turnOffAllNotes();
    }
  }

  //reset our state
  lastTouched = currTouched;
  lastFlow = flow;

  delay(100);

  //Disable interrupts
  cli();
  calculateFlow();
}

// Read all of the inputs
void readInputs()
{
  // Get the currently touched pads
  currTouched = cap.touched();

  joystick_x_position = analogRead(PIN_ANALOG_X);
  joystick_y_position = analogRead(PIN_ANALOG_Y);
  joystick_buttonState = digitalRead(JOYSTICK_BUTTON);
}

// Toggle the LED based on fingering combination
void checkForValidCombo(uint16_t reading)
{
  // Retrieve the bottom 8 bits from the reading
  uint8_t temp = (reading & 0xff);
  for (int i = 0; i < 10; i++)
  {
    if (temp == noteButtons[i])
    {
      digitalWrite(PIN_LED_INDICATOR, HIGH);
      validNote = true;
    }
  }

  if (validNote == false)
  {
    digitalWrite(PIN_LED_INDICATOR, LOW);
  } else
  {
    validNote = false;
  }
}

// Determine joystick position based off of x-position and y-position values
void findJoystickDirection()
{
  if (joystick_x_position > 630 && 400 < joystick_y_position < 800 ) {
    currJoystickPosition = 4;
  } else if (joystick_x_position < 250 && 200 < joystick_y_position < 800) {
    currJoystickPosition = 3;
  } else if (joystick_y_position > 630 && 200 < joystick_x_position < 600) {
    currJoystickPosition = 1;
  } else if (joystick_y_position < 250 && 300 < joystick_x_position < 800) {
    currJoystickPosition = 2;
  } else {
    currJoystickPosition = 0;
  }
}

// If joystick position has changed, return the new joystick position value
// Otherwise, return -1
int didJoystickChange()
{
  if (currJoystickPosition != lastJoystickPosition) {
    lastJoystickPosition = currJoystickPosition;

    if (lastJoystickPosition != 0) {
      return lastJoystickPosition;
    }
  }
  return -1;
}

// If joystick is left or right -> enable vibratto
void changeVibrato(int joystickPosition)
{
  if (lastJoystickPosition == 3 || lastJoystickPosition == 4)
  {
    enableVibrato();

    if (vibratoEnabled == false)
    {
      noteStartTime = millis();
    }

    vibratoEnabled = true;
  } else
  {
    if (vibratoEnabled == true)
    {
      disableVibrato();
      vibratoEnabled = false;
    }
  }
}

// If the joystick position is up, increase octave
// If the joystick position is down, descrease octave
// Note: Currently support 7 octaves (-3...3)
void changeOctave(int joystickPosition)
{
  if (lastJoystickPosition == 2 && currOctave < 3)
  {
    Serial.print("Octave Up: ");
    Serial.println(currOctave + 1);
    currOctave++;
    turnOffAllNotes();
  } else if (lastJoystickPosition == 1 && currOctave > -3)
  {
    Serial.print("Octave Down: ");
    Serial.println(currOctave - 1);
    currOctave--;
    turnOffAllNotes();
  }
}

// Enable double tonguing mode if joystick button is pressed
void checkForDoubleTonguing() {
  if (!joystick_buttonState) {
    turnOffAllNotes();
  }
}

// Calculate flow based off rising edges from flow sensor
void calculateFlow()
{
  flow = (NbTopsFan * 60 / 7.5);
}

// Return difference in flow from last state to current state
int changeInFlow()
{
  return lastFlow - flow;
}

// Handles all logic pertaining to playing a note
// Sends MIDI message to turn note on if the user is in the corrrect position and it it isn't already on
// Sends MIDI message to turn note off if the user has changed their finger position
void playNote (uint16_t reading)
{
  // Retrieve the bottom 8 bits from the reading
  uint8_t temp = (reading & 0xff);

  for (int i = 0; i < 10; i++)
  {
    // Calculates the current MIDI note by multipling the supported MIDI notes by the currOctave multiplier
    int tempNoteNumber = noteMIDI[i] + (12 * currOctave);
    if (temp == noteButtons[i] && notesEnabled[tempNoteNumber] == false)
    {
      usbMIDI.sendNoteOn(tempNoteNumber, 99, MIDI_CHANNEL);
      notesEnabled[tempNoteNumber] = true;
    }
    else if (temp != noteButtons[i] && notesEnabled[tempNoteNumber] == true)
    {
      usbMIDI.sendNoteOff(tempNoteNumber, 99, MIDI_CHANNEL);
      notesEnabled[tempNoteNumber] = false;
    }
  }
}

// Iterates through each of the MIDI notes and turns them all off
void turnOffAllNotes()
{
  for (uint8_t i = 0; i < numberOfNotes; i++)
  {
    if (notesEnabled[i])
    {
      usbMIDI.sendNoteOff(i, 0, MIDI_CHANNEL);
      notesEnabled[i] = false;
    }
  }
}

// Check to see if value has changed
bool shouldPrint(int num, int lastNum)
{
  if (num != lastNum)
  {
    return true;
  }
  return false;
}

// Turn Vibrato On
// Vibrato value is based off of joystick position as well as time to give cool sound effect
void enableVibrato()
{
  Serial.println("Vibrato Mode Enabled");
  vibratoValue = (joystick_x_position - 512) * 4 + 2048 * sin(((millis() - noteStartTime) * 0.001 * 2 * PI));
  int temp = (int) vibratoValue;
  usbMIDI.sendPitchBend(temp, MIDI_CHANNEL);
}

// Reset the MIDI PitchBend to 0 to disable vibrato
void disableVibrato()
{
  usbMIDI.sendPitchBend(0, MIDI_CHANNEL);
}

// Used for keeping track of the flow sensor value
void incrementCount()
{
  NbTopsFan++;  //This function measures the rising and falling edge of the hall effect sensors signal
}
