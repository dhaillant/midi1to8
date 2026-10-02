/*
 * Simple MIDI Thru
 *
 * All MIDI messages are transmitted on outputs 1 to 8
 *
 * Byte-level pass-through: every byte received on the UART is pushed straight
 * back out. There is no MIDI parser, and therefore no parser state that can
 * get stuck.
 *
 * Why the Arduino MIDI Library is not used here:
 * its parser has a one-way trap. Once it sees an 0xF0 - a real SysEx, or just
 * one corrupted byte on the input line - it stays in SysEx mode until an 0xF7
 * arrives. While in that state, Note On/Off/CC status bytes are swallowed into
 * the SysEx buffer instead of being forwarded. Real-time bytes (Clock, Start,
 * Stop) bypass the state machine, so the module keeps passing clock and the
 * LED keeps blinking while every note silently disappears, until it is power
 * cycled. A thru does not need to understand the messages it forwards, so it
 * does not need a parser at all.
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


// MIDI baud rate
#define MIDI_BAUD 31250

// Blink the LED on musical data only. MIDI clock is sent 24 times per quarter
// note, so counting it here would keep the LED permanently lit while a
// sequencer is running. Set to 0 to blink on every byte instead.
#define BLINK_ON_REALTIME 0

// Optional hardware watchdog.
// The byte router has no state of its own that can lock up, so there is
// nothing left for a software "unstick" timer to reset. What is still worth
// guarding against is the CPU itself stopping - a brown-out while patching, a
// glitch that corrupts the stack. The WDT resets the board if loop() stops
// running for 500 ms.
//
// WARNING: only enable this if the board has the Optiboot bootloader (Arduino
// Uno, or a Nano burned as "ATmega328P" rather than "ATmega328P (Old
// Bootloader)"). The old ATmegaBOOT bootloader does not clear the watchdog
// reset flag at startup, so the board would reset in a loop and could only be
// recovered with an ISP programmer. If unsure, leave this at 0.
#define USE_WATCHDOG 0

#if USE_WATCHDOG
  #include <avr/wdt.h>
#endif


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

  // Open the UART at MIDI baud rate. RX is the MIDI input, TX feeds all 8
  // output gates in parallel.
  Serial.begin(MIDI_BAUD);

#if USE_WATCHDOG
  // Armed last, so it cannot fire during the startup LED animation above
  wdt_enable(WDTO_500MS);
#endif
}

void loop()
{
  // Forward every incoming byte as it arrives.
  // No buffering, no parsing: a byte in is a byte out, so nothing here can
  // desynchronise or latch into a bad state. This also removes the
  // store-and-forward latency of waiting for a complete 3-byte message.
  //
  // Serial.flush() is not needed: the output gates are wired permanently open
  // in setup(), so there is nothing to wait for before closing them.
  while (Serial.available())
  {
    byte b = Serial.read();
    Serial.write(b);

#if BLINK_ON_REALTIME
    blink_MIDI_LED();
#else
    if (b < 0xF8) blink_MIDI_LED();   // skip Clock and the other real-time bytes
#endif
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

#if USE_WATCHDOG
  wdt_reset();
#endif
}

void render_MIDI_LED()
{
  digitalWrite(MIDI_LED,MIDI_blink_counter > 0 ? LOW : HIGH);
}
