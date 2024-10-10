/*
 * One Output per MIDI Channel
 * 
 * MIDI messages are splitted and transmitted on outputs 1 to 8, according to their MIDI channel.
 * 
 * Example: if a NoteOn message is received on MIDI channel 2, Output 2 will transmit the NoteOn message
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
bool needs_refresh;
uint8_t MIDI_blink;


// -----------------------------------------------------------------------------

volatile uint8_t control_clock_tick;

ISR(TIMER0_COMPA_vect) {
  // 1kHz clock for timing trigger pulses.
  ++control_clock_tick;
}

void tick()
{
  if (MIDI_blink)
  {
    --MIDI_blink;
    needs_refresh = true;
  }
}

#define MIDI_LED_BLINK_TIME 20
void blink_MIDI_LED(void)
{
  MIDI_blink = MIDI_LED_BLINK_TIME;
}




void setup()
{
  // set Inputs
  pinMode(MIDI_IN, INPUT);

  // set Outputs
  pinMode (MIDI_LED, OUTPUT);
  pinMode (MIDI_OUT, OUTPUT);

  // initialize MIDI LED state (off) and blink
  for (uint8_t i = 0; i < 4; i++)
  {
    digitalWrite(MIDI_LED, LOW);
    delay(50);
    digitalWrite(MIDI_LED, HIGH);
    delay(100);
  }

  // MIDI outputs
  for (byte i = 0; i < NBR_MIDI_OUTS; i++)
  {
    pinMode(midi_out_pins[i],       OUTPUT);
    digitalWrite(midi_out_pins[i],  HIGH);    // enable on LOW
  }

  // Set a 1kHz timer for non-blocking Trigger and blinking durations/delays
  TCCR0A |= (1 << WGM01);                       // Set Timer0 to CTC (Clear Timer on Compare Match) mode
  TCCR0B |= (1 << CS01) | (1 << CS00);          // Set prescaler to 64 (prescaler bits: CS02=0, CS01=1, CS00=1)
  OCR0A = 124; // (16MHz / (64 * 1000Hz)) - 1   // Set compare match register to generate a 1kHz frequency
  TIMSK0 |= (1 << OCIE0A);                      // Enable timer compare interrupt
  sei();                                        // Enable interrupts

  MIDI.turnThruOff();
  
  // Initiate MIDI communications, listen to ALL channels
  MIDI.begin(MIDI_CHANNEL);
}

void loop()
{
  // read incomming MIDI messages
  if (MIDI.read())
  {
    uint8_t channel = MIDI.getChannel();

    if ((channel > 0) && (channel <= 8))
    {
      // enable the output corresponding to the incoming MIDI message channel
      digitalWrite(midi_out_pins[channel - 1], LOW);

      // write the message back
      MIDI.send(MIDI.getType(),
                MIDI.getData1(),
                MIDI.getData2(),
                channel);
      
      // wait until transmit buffer is empty
      Serial.flush();

      // disable the output
      digitalWrite(midi_out_pins[channel - 1], HIGH);

      blink_MIDI_LED();
    }
  }


  // update MIDI LED if required
  if (needs_refresh)
  {
    render_MIDI_LED();
    needs_refresh = false;
  }

  if (control_clock_tick)
  {
    --control_clock_tick;
    tick();
  }
}

void render_MIDI_LED()
{
  digitalWrite(MIDI_LED,MIDI_blink > 0 ? LOW : HIGH);
}
