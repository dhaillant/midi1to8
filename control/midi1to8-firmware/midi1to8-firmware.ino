/*
 * MIDI 1-to-8 Router with SysEx Configuration
 * 
 * Routes MIDI messages from 1 input to 8 outputs based on channel matrix
 * Configuration via SysEx messages
 * 8 presets (configurations) selectable via Program Change on channel 16
 * 
 * Requires the Arduino MIDI Library:
 * https://github.com/FortySevenEffects/arduino_midi_library
 */

// CRITICAL: Define buffer size BEFORE including MIDI.h
// Our SysEx messages are 25 bytes, so we need at least 32 bytes buffer
#define MIDI_SYSEX_ARRAY_SIZE 64

#include "sysex_handling.h"
#include <MIDI.h>
#include "MidiInputGuard.h"
#include <EEPROM.h>

// ============================================================================
// Pin Definitions
// ============================================================================
#define MIDI_LED A0
#define MIDI_IN 0
#define MIDI_OUT 1
#define DEBUG_LED 13

#define NBR_MIDI_OUTS 8

byte midi_out_pins[NBR_MIDI_OUTS] = {
  2, 3, 4, 5, 6, 7, 8, 9  // Outputs 1-6 on PORTD, 7-8 on PORTB
};

// ============================================================================
// Port Manipulation Masks (for fast GPIO)
// ============================================================================
#define MIDI_OUT_MASK_D 0b11111100  // bits 2-7 of PORTD (outputs 1-6)
#define MIDI_OUT_MASK_B 0b00000011  // bits 0-1 of PORTB (outputs 7-8)

// Output 8 is dedicated to SysEx responses (module -> PC communication)
#define SYSEX_OUTPUT_MASK 0x80  // Output 8 only

// ============================================================================
// MIDI Configuration
// ============================================================================
#define MIDI_CHANNEL MIDI_CHANNEL_OMNI
#define PRESET_CHANGE_CHANNEL 16  // Channel 16 for preset switching
// The guard keeps a corrupt or truncated SysEx from locking up the parser
// (see MidiInputGuard.h)
MidiInputGuard<HardwareSerial> guardedSerial(Serial);
midi::MidiInterface<MidiInputGuard<HardwareSerial>> MIDI(guardedSerial);

// ============================================================================
// Multi-Preset Support
// ============================================================================
#define NBR_PRESETS 8
#define MATRIX_SIZE 17

// Current preset and matrix
byte current_preset = 0;
byte matrix[MATRIX_SIZE];

// ============================================================================
// EEPROM Configuration
// ============================================================================
// Memory layout:
// Address 0-135:   Preset 0-7 matrices (17 bytes each)
// Address 136:     Current preset number
// Address 137:     Magic number (0xA5)

#define EEPROM_PRESETS_START 0
#define EEPROM_CURRENT_PRESET 136
#define EEPROM_MAGIC_ADDR 137
#define EEPROM_MAGIC 0xA5

// ============================================================================
// LED Blink Management
// ============================================================================
bool MIDI_LED_needs_refresh;
uint8_t MIDI_blink_counter;
#define MIDI_LED_BLINK_TIME 20

// ============================================================================
// 1kHz Timer for LED blinking
// ============================================================================
volatile uint8_t control_clock_tick;

ISR(TIMER0_COMPA_vect) {
  ++control_clock_tick;
}

void tick() {
  if (MIDI_blink_counter) {
    --MIDI_blink_counter;
  } else {
    MIDI_LED_needs_refresh = true;
  }
}

void blink_MIDI_LED(void) {
  MIDI_blink_counter = MIDI_LED_BLINK_TIME;
  MIDI_LED_needs_refresh = true;
}

void render_MIDI_LED() {
  digitalWrite(MIDI_LED, MIDI_blink_counter > 0 ? LOW : HIGH);
}

// ============================================================================
// Preset Indicator (blink LED to show preset number)
// ============================================================================
void indicate_preset(byte preset_num) {
  // Blink LED (preset_num + 1) times to show which preset is active
  // Preset 0 = 1 blink, Preset 7 = 8 blinks
  for (byte i = 0; i < preset_num + 1; i++) {
    digitalWrite(MIDI_LED, LOW);   // LED ON
    delay(150);
    digitalWrite(MIDI_LED, HIGH);  // LED OFF
    delay(150);
  }
}

// ============================================================================
// Fast Output Control using Port Manipulation
// ============================================================================
inline void setMidiOutputs(uint8_t mask) {
  // mask: bit 0 = output 1, bit 7 = output 8
  // Invert because logic gates activate on LOW
  mask = ~mask;
  
  // Outputs 1-6 on PORTD (bits 2-7)
  PORTD = (PORTD & ~MIDI_OUT_MASK_D) | ((mask & 0b00111111) << 2);
  
  // Outputs 7-8 on PORTB (bits 0-1)
  PORTB = (PORTB & ~MIDI_OUT_MASK_B) | ((mask >> 6) & 0b00000011);
}

// ============================================================================
// EEPROM Functions
// ============================================================================

void save_preset_to_EEPROM(byte preset_num, byte* preset_matrix) {
  // Calculate start address for this preset
  int start_addr = EEPROM_PRESETS_START + (preset_num * MATRIX_SIZE);
  
  // Save matrix data
  for (unsigned int i = 0; i < MATRIX_SIZE; i++) {
    EEPROM.update(start_addr + i, preset_matrix[i]);
  }
}

void read_preset_from_EEPROM(byte preset_num, byte* preset_matrix) {
  // Calculate start address for this preset
  int start_addr = EEPROM_PRESETS_START + (preset_num * MATRIX_SIZE);
  
  // Read matrix data
  for (unsigned int i = 0; i < MATRIX_SIZE; i++) {
    preset_matrix[i] = EEPROM.read(start_addr + i);
  }
}

void save_current_preset_number() {
  EEPROM.update(EEPROM_CURRENT_PRESET, current_preset);
}

byte read_current_preset_number() {
  byte preset = EEPROM.read(EEPROM_CURRENT_PRESET);
  // Validate preset number
  if (preset >= NBR_PRESETS) {
    preset = 0;
  }
  return preset;
}

void initialize_EEPROM_if_needed() {
  // Check if EEPROM contains valid data
  if (EEPROM.read(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC) {
    // First time initialization: set all presets to full routing
    byte default_matrix[MATRIX_SIZE];
    for (unsigned int i = 0; i < MATRIX_SIZE; i++) {
      default_matrix[i] = 0xFF;  // All outputs active
    }
    
    // Save default preset to all slots
    for (byte preset = 0; preset < NBR_PRESETS; preset++) {
      save_preset_to_EEPROM(preset, default_matrix);
    }
    
    // Set current preset to 0
    EEPROM.update(EEPROM_CURRENT_PRESET, 0);
    
    // Write magic number
    EEPROM.update(EEPROM_MAGIC_ADDR, EEPROM_MAGIC);
  }
}

void load_current_preset() {
  read_preset_from_EEPROM(current_preset, matrix);
}

void switch_preset(byte new_preset) {
  if (new_preset >= NBR_PRESETS) {
    return;  // Invalid preset number
  }
  
  current_preset = new_preset;
  load_current_preset();
  save_current_preset_number();
  
  // Visual feedback: blink LED to show preset number
  indicate_preset(current_preset);
}

// ============================================================================
// MIDI Message Routing
// ============================================================================

void routeMidiMessage(midi::MidiType type, byte data1, byte data2, byte channel) {
  uint8_t outputMask = 0;
  
  // Determine which outputs should receive this message
  // Real-Time messages are in range 0xF8-0xFF (Clock=248 to SystemReset=255)
  if (type >= 248) {  // midi::Clock and above
    outputMask = matrix[16];
  } else if (channel >= 1 && channel <= 16) {
    // Channel messages (0x80-0xEF)
    outputMask = matrix[channel - 1];
  } else {
    // Other system messages: do not route
    return;
  }
  
  // If no outputs are active, don't send anything
  if (outputMask == 0) return;
  
  // Activate concerned outputs
  setMidiOutputs(outputMask);
  
  // Send MIDI message using the MIDI library (properly formats the message)
  MIDI.send(type, data1, data2, channel);
  
  // Wait until transmit buffer is empty before disabling outputs
  Serial.flush();
  
  // Deactivate all outputs after sending
  setMidiOutputs(0x00);
}

// ============================================================================
// MIDI Callbacks
// ============================================================================

void handleNoteOn(byte channel, byte note, byte velocity) {
  routeMidiMessage(midi::NoteOn, note, velocity, channel);
}

void handleNoteOff(byte channel, byte note, byte velocity) {
  routeMidiMessage(midi::NoteOff, note, velocity, channel);
}

void handleAfterTouchPoly(byte channel, byte note, byte pressure) {
  routeMidiMessage(midi::AfterTouchPoly, note, pressure, channel);
}

void handleControlChange(byte channel, byte number, byte value) {
  routeMidiMessage(midi::ControlChange, number, value, channel);
}

void handleProgramChange(byte channel, byte number) {
  // Program Change on channel 16 switches internal routing preset
  if (channel == PRESET_CHANGE_CHANNEL && number < NBR_PRESETS) {
    switch_preset(number);
  }
  
  // Always forward Program Change to outputs
  routeMidiMessage(midi::ProgramChange, number, 0, channel);
}

void handleAfterTouchChannel(byte channel, byte pressure) {
  routeMidiMessage(midi::AfterTouchChannel, pressure, 0, channel);
}

void handlePitchBend(byte channel, int bend) {
  routeMidiMessage(midi::PitchBend, bend & 0x7F, (bend >> 7) & 0x7F, channel);
}

void handleClock() {
  routeMidiMessage(midi::Clock, 0, 0, 0);
}

void handleStart() {
  routeMidiMessage(midi::Start, 0, 0, 0);
}

void handleContinue() {
  routeMidiMessage(midi::Continue, 0, 0, 0);
}

void handleStop() {
  routeMidiMessage(midi::Stop, 0, 0, 0);
}

void handleActiveSensing() {
  routeMidiMessage(midi::ActiveSensing, 0, 0, 0);
}

void handleSystemReset() {
  routeMidiMessage(midi::SystemReset, 0, 0, 0);
}

// ============================================================================
// SysEx Message Handlers
// ============================================================================

void read_preset_from_device(byte preset_num) {
  // Read specified preset from EEPROM
  byte temp_matrix[MATRIX_SIZE];
  read_preset_from_EEPROM(preset_num, temp_matrix);
  
  // Build complete SysEx message WITH F0 and F7
  // Structure: F0 7D 18 00 02 [preset_num] [20 encoded bytes] F7
  byte sysex_message[27];  // F0 + 4 header + 1 preset + 20 data + F7
  
  sysex_message[0] = SYSEX_START;
  sysex_message[1] = MANUFACTURER;
  sysex_message[2] = MODEL;
  sysex_message[3] = MANAGER;
  sysex_message[4] = COMMAND_READ_PRESET;
  sysex_message[5] = preset_num;
  
  // Encode matrix to 7-bit (20 bytes) starting at index 6
  convert_to_7bit_message(temp_matrix, MATRIX_SIZE, &sysex_message[6]);
  
  sysex_message[26] = SYSEX_END;
  
  // Send via output 8 using MIDI library
  // inArrayContainsBoundaries = true because we included F0 and F7
  setMidiOutputs(SYSEX_OUTPUT_MASK);
  MIDI.sendSysEx(27, sysex_message, true);
  Serial.flush();
  setMidiOutputs(0x00);
}

void write_preset_to_device(const byte* data, unsigned int length, byte target_preset) {
  // Validate preset number
  if (target_preset >= NBR_PRESETS) {
    return;
  }
  
  // Validate data length (should be 20 bytes for 7-bit encoded 17 bytes)
  if (length != 20) {
    return;
  }
  
  // Decode and save the matrix
  byte temp_matrix[MATRIX_SIZE];
  convert_from_7bit_message(data, length, temp_matrix);
  
  // Save to EEPROM
  save_preset_to_EEPROM(target_preset, temp_matrix);
  
  // If writing to current preset, reload it
  if (target_preset == current_preset) {
    load_current_preset();
  }
  
  // Visual feedback - brief flash on DEBUG_LED (non-blocking)
  digitalWrite(DEBUG_LED, HIGH);
  delayMicroseconds(500);  // 500µs flash - very brief
  digitalWrite(DEBUG_LED, LOW);
}

void forward_sysex_to_all_outputs(const byte* data, unsigned int length) {
  // Forward incoming SysEx to all outputs (for chained modules)
  // Activate all outputs
  setMidiOutputs(0xFF);
  
  // Reconstruct and send the complete SysEx message
  Serial.write(SYSEX_START);
  for (unsigned int i = 0; i < length; i++) {
    Serial.write(data[i]);
  }
  Serial.write(SYSEX_END);
  
  // Wait until transmit buffer is empty before disabling outputs
  Serial.flush();
  
  // Deactivate outputs
  setMidiOutputs(0x00);
}

void handleSysEx(byte* data, unsigned int length) {
  // SysEx data includes F0 at start (and F7 at end in length)
  // So: data[0] = F0, data[1] = MANUFACTURER, data[2] = MODEL, etc.
  
  // Validate minimum length (F0 + 4 header bytes = 5 minimum)
  if (length < 5) {
    return;
  }

  // A SysEx longer than the library's buffer arrives in chunks
  // (F0 ... F0, F7 ... F0, F7 ... F7): only accept a message that is whole.
  if (data[0] != SYSEX_START || data[length - 1] != SYSEX_END) {
    return;
  }
  
  // Check manufacturer (at index 1, after F0)
  if (data[1] != MANUFACTURER) {
    return;
  }
  
  // Check model (at index 2)
  if (data[2] != MODEL) {
    return;
  }
  
  // Check device_id (at index 3)
  byte device_id = data[3];
  bool is_for_us = (device_id == DEVICE || device_id == MANAGER || device_id == 0x7F);
  
  // Only forward messages that are NOT for us
  if (!is_for_us) {
    // For forwarding, skip first byte (F0) and last byte (F7)
    forward_sysex_to_all_outputs(&data[1], length - 2);
    return;
  }
  
  // Message is for us, process it
  byte command = data[4];  // Command is at index 4 (after F0, MANUF, MODEL, DEVICE)
  
  switch (command) {
    case COMMAND_PING_DEVICE:
      // Respond to ping with current preset configuration
      read_preset_from_device(current_preset);
      break;
      
    case COMMAND_READ_PRESET:
      // Read specific preset
      // Data: [F0][7D][18][01][02][preset_num]...
      // So preset_num is at index 5
      if (length >= 6) {
        byte preset_num = data[5];
        if (preset_num < NBR_PRESETS) {
          read_preset_from_device(preset_num);
        }
      }
      break;
      
    case COMMAND_WRITE_PRESET:
      // Write specific preset
      // Data: [F0][7D][18][01][03][preset_num][20 bytes packed data][F7]
      // So preset_num at index 5, packed data starts at index 6
      // Total length should be: 1(F0) + 4(header) + 1(preset) + 20(data) + 1(F7) = 27
      // Exactly 27: a message cut short arrives closed by MidiInputGuard's F7.
      if (length == 27) {
        byte target_preset = data[5];
        write_preset_to_device(&data[6], 20, target_preset);
      }
      break;
      
    case COMMAND_SWITCH_PRESET:
      // Switch active preset (same as Program Change on channel 16)
      if (length >= 6) {
        byte new_preset = data[5];
        switch_preset(new_preset);
      }
      break;
      
    default:
      break;
  }
}

// ============================================================================
// Setup
// ============================================================================

void setup() {
  // Configure pins
  pinMode(MIDI_IN, INPUT);
  pinMode(MIDI_LED, OUTPUT);
  pinMode(MIDI_OUT, OUTPUT);
  pinMode(DEBUG_LED, OUTPUT);

  // MIDI outputs
  for (byte i = 0; i < NBR_MIDI_OUTS; i++) {
    pinMode(midi_out_pins[i], OUTPUT);
    digitalWrite(midi_out_pins[i], HIGH);  // Deactivate (HIGH = gate closed)
  }

  // Startup LED animation
  for (uint8_t i = 0; i < 4; i++) {
    digitalWrite(MIDI_LED, LOW);
    delay(50);
    digitalWrite(MIDI_LED, HIGH);
    delay(100);
  }

  // Configure 1kHz timer for LED blinking
  TCCR0A |= (1 << WGM01);
  TCCR0B |= (1 << CS01) | (1 << CS00);
  OCR0A = 124;
  TIMSK0 |= (1 << OCIE0A);
  sei();

  // Initialize EEPROM if needed (first boot)
  initialize_EEPROM_if_needed();
  
  // Load current preset number from EEPROM
  current_preset = read_current_preset_number();
  
  // Load the current preset's matrix
  load_current_preset();
  
  // Show which preset is loaded
  indicate_preset(current_preset);

  // Configure MIDI
  MIDI.turnThruOff();  // Disable automatic THRU - we handle routing manually
  
  // Register callbacks
  MIDI.setHandleNoteOn(handleNoteOn);
  MIDI.setHandleNoteOff(handleNoteOff);
  MIDI.setHandleAfterTouchPoly(handleAfterTouchPoly);
  MIDI.setHandleControlChange(handleControlChange);
  MIDI.setHandleProgramChange(handleProgramChange);
  MIDI.setHandleAfterTouchChannel(handleAfterTouchChannel);
  MIDI.setHandlePitchBend(handlePitchBend);
  MIDI.setHandleClock(handleClock);
  MIDI.setHandleStart(handleStart);
  MIDI.setHandleContinue(handleContinue);
  MIDI.setHandleStop(handleStop);
  MIDI.setHandleActiveSensing(handleActiveSensing);
  MIDI.setHandleSystemReset(handleSystemReset);
  MIDI.setHandleSystemExclusive(handleSysEx);

  MIDI.begin(MIDI_CHANNEL);
}

// ============================================================================
// Main Loop
// ============================================================================

void loop() {
  // Read incoming MIDI messages
  if (MIDI.read()) {
    blink_MIDI_LED();
  }

  // Update MIDI LED
  if (MIDI_LED_needs_refresh) {
    render_MIDI_LED();
    MIDI_LED_needs_refresh = false;
  }

  // Timer management
  if (control_clock_tick) {
    --control_clock_tick;
    tick();
  }
}
