/*
 * Simple MIDI Thru
 * 
 * All MIDI messages are transmitted on outputs 1 to 8
 * 
 * 
 * Requires the Arduino MIDI Library:
 * https://github.com/FortySevenEffects/arduino_midi_library
 * 
*/

  // Pin definitions
  #define MIDI_LED A0
  
  #define MIDI_IN 0
  #define MIDI_OUT 1
  
  #define NBR_MIDI_OUTS 8
  
  byte midi_out_pins[NBR_MIDI_OUTS] = {
    2, 3, 4, 5, 6, 7, 8, 9
  };    // array of output pin numbers (Arduino #)


#include <MIDI.h>

// MIDI Channel we want to react to
#define MIDI_CHANNEL MIDI_CHANNEL_OMNI

MIDI_CREATE_DEFAULT_INSTANCE();


// blink stuff for input
bool MIDI_LED_needs_refresh;
uint8_t MIDI_blink_counter;


// -----------------------------------------------------------------------------

volatile uint8_t control_clock_tick;

ISR(TIMER0_COMPA_vect) {
  // 1kHz clock for timing trigger pulses.
  ++control_clock_tick;
}

void tick()
{
  // if MIDI_blink_counter is not 0
  if (MIDI_blink_counter)
  {
    --MIDI_blink_counter;
  }
  else
  {
    // MIDI_blink_counter is 0 so we can request a refresh of the outputs
    MIDI_LED_needs_refresh = true;
  }
}

#define MIDI_LED_BLINK_TIME 20

// start MIDI LED timer
void blink_MIDI_LED(void)
{
  MIDI_blink_counter = MIDI_LED_BLINK_TIME;
  MIDI_LED_needs_refresh = true;
}




void setup()
{
  // set Inputs
  pinMode(MIDI_IN, INPUT);

  // set Outputs
  pinMode (MIDI_LED, OUTPUT);
  pinMode (MIDI_OUT, OUTPUT);

  // MIDI outputs
  for (byte i = 0; i < NBR_MIDI_OUTS; i++)
  {
    pinMode(midi_out_pins[i],       OUTPUT);
    digitalWrite(midi_out_pins[i],  LOW);    // enable on LOW
  }

  // initialize MIDI LED state (off) and blink
  for (uint8_t i = 0; i < 4; i++)
  {
    digitalWrite(MIDI_LED, LOW);
    delay(50);
    digitalWrite(MIDI_LED, HIGH);
    delay(100);
  }


  // Set a 1kHz timer for non-blocking Trigger and blinking durations/delays
  TCCR0A |= (1 << WGM01);                       // Set Timer0 to CTC (Clear Timer on Compare Match) mode
  TCCR0B |= (1 << CS01) | (1 << CS00);          // Set prescaler to 64 (prescaler bits: CS02=0, CS01=1, CS00=1)
  OCR0A = 124; // (16MHz / (64 * 1000Hz)) - 1   // Set compare match register to generate a 1kHz frequency
  TIMSK0 |= (1 << OCIE0A);                      // Enable timer compare interrupt
  sei();                                        // Enable interrupts

  MIDI.turnThruOn();

  // Initiate MIDI communications, listen to ALL channels
  MIDI.begin(MIDI_CHANNEL);
}

void loop()
{
  // read incomming MIDI messages
  if (MIDI.read())
  {
    blink_MIDI_LED();
    // wait until transmit buffer is empty
    //Serial.flush();
  }

  // update MIDI LED if required
  if (MIDI_LED_needs_refresh)
  {
    render_MIDI_LED();
    MIDI_LED_needs_refresh = false;
  }

  if (control_clock_tick)
  {
    --control_clock_tick;
    tick();
  }
}

void render_MIDI_LED()
{
  digitalWrite(MIDI_LED,MIDI_blink_counter > 0 ? LOW : HIGH);
}
