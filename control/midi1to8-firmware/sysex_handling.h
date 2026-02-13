/*
 * SysEx Handling Functions for MIDI 1-to-8 Router
 * 
 * Provides encoding/decoding of 8-bit data to/from 7-bit MIDI SysEx format
 */

#ifndef SYSEX_HANDLING_H
#define SYSEX_HANDLING_H

#include <Arduino.h>

// ============================================================================
// SysEx Protocol Definitions
// ============================================================================
#define SYSEX_START   0xF0
#define SYSEX_END     0xF7
#define MANUFACTURER  0x7D  // Non-official ID for experiments and DIY
#define MODEL         0x18  // MIDI 1-8
#define DEVICE        0x01  // First device (configurable for chaining)
#define MANAGER       0x00  // Manager for answering

// Commands
#define COMMAND_PING_DEVICE        0x01
#define COMMAND_READ_PRESET        0x02  // Read single preset (configuration)
#define COMMAND_WRITE_PRESET       0x03  // Write single preset
#define COMMAND_CHANGE_DEVICE_ID   0x04
#define COMMAND_SWITCH_PRESET      0x05  // Switch active preset (like Program Change on channel 16)
// Note: Reading/writing all presets = sending 8 × READ/WRITE_PRESET commands

// Header structure (without F0 which is handled by MIDI library)
#define SYSEX_HEADER_SIZE  4  // MANUFACTURER, MODEL, DEVICE, COMMAND

// ============================================================================
// Debug Configuration
// ============================================================================
#define DEBUG 0

#if DEBUG
void printHexWithLeadingZero(byte value) {
  if (value < 0x10) {
    Serial.print("0");
  }
  Serial.print(value, HEX);
}
#endif

// ============================================================================
// SysEx Message Construction
// ============================================================================

/**
 * Construct a complete SysEx message
 * 
 * @param sysex_header Message header (including F0)
 * @param header_length Header length
 * @param packed_message 7-bit encoded data
 * @param packed_length Encoded data length
 * @param message_length [OUT] Total constructed message length
 * @return Pointer to allocated message (must be freed with free())
 */
byte* construct_sysex_message(const byte* sysex_header, uint8_t header_length, 
                              const byte* packed_message, uint8_t packed_length, 
                              uint8_t &message_length) {
  #if DEBUG
    Serial.print("\n\nsysex_header length: ");
    Serial.print(header_length);
    Serial.print("\npacked_message length: ");
    Serial.print(packed_length);
  #endif
  
  message_length = header_length + packed_length + 1;  // +1 for SYSEX_END
  byte* sysex_message = (byte*)malloc(message_length);
  
  if (sysex_message == NULL) {
    message_length = 0;
    return NULL;
  }
  
  #if DEBUG
    Serial.print("\nsysex_message length: ");
    Serial.println(message_length);
  #endif
  
  // Copy header
  for (uint8_t i = 0; i < header_length; i++) {
    sysex_message[i] = sysex_header[i];
  }
  
  // Copy data
  for (uint8_t i = 0; i < packed_length; i++) {
    sysex_message[i + header_length] = packed_message[i];
  }
  
  // Add end byte
  sysex_message[message_length - 1] = SYSEX_END;
  
  return sysex_message;
}

// ============================================================================
// 7-bit Encoding/Decoding Functions
// ============================================================================

/**
 * Encode 8-bit bytes into 7-bit format for SysEx
 * 
 * Format: For N input bytes, generates ceil(N*8/7) output bytes
 * - The 7 LSBs of each byte are stored directly
 * - MSBs are collected in "carry" bytes every 7 bytes
 * 
 * Structure for 17 input bytes (→ 20 output bytes):
 * [carry0][data0-6][carry1][data7-13][carry2][data14-16]
 * 
 * @param input_bytes 8-bit data to encode
 * @param input_length Number of bytes to encode
 * @param packed_message Output buffer (must be >= ceil(input_length*8/7))
 */
void convert_to_7bit_message(const byte* input_bytes, uint8_t input_length, 
                             byte* packed_message) {
  byte carry = 0x00;
  uint8_t carry_idx = 0;
  uint8_t packed_idx = 1;
  uint8_t carry_cnt = 0;
  
  for (uint8_t i = 0; i < input_length; i++) {
    // Store the 7 LSBs
    packed_message[packed_idx] = input_bytes[i] & 0x7F;
    packed_idx++;
    
    // Collect MSB in carry
    // First MSB goes to bit 0 of carry, second to bit 1, etc.
    carry |= ((input_bytes[i] & 0x80) >> 7) << carry_cnt;
    carry_cnt++;
    
    // Every 7 bytes, save the carry and restart
    if (carry_cnt == 7) {
      carry_cnt = 0;
      packed_message[carry_idx] = carry;
      carry = 0x00;
      carry_idx = packed_idx;
      packed_idx++;
    }
  }
  
  // Save last carry (even if not complete)
  packed_message[carry_idx] = carry;
}

/**
 * Decode 7-bit SysEx data back to 8-bit
 * 
 * @param packed_message 7-bit encoded data
 * @param packed_length Encoded data length
 * @param retrieved_bytes Output buffer for 8-bit bytes
 */
void convert_from_7bit_message(const byte* packed_message, uint8_t packed_length, 
                               byte* retrieved_bytes) {
  uint8_t byte_idx = 0;
  uint8_t carry_cnt = 0;
  byte carry = 0x00;
  
  for (uint8_t i = 0; i < packed_length; i++) {
    if (carry_cnt == 0) {
      // First byte of a group: it's the carry
      carry = packed_message[i];
    } else {
      // Reconstruct byte: 7 LSBs + MSB from carry
      retrieved_bytes[byte_idx] = packed_message[i];  // 7 LSBs
      retrieved_bytes[byte_idx] |= ((carry >> (carry_cnt - 1)) & 0x01) << 7;  // MSB
      byte_idx++;
    }
    
    carry_cnt++;
    
    // Every 8 bytes (1 carry + 7 data), restart
    if (carry_cnt == 8) {
      carry_cnt = 0;
    }
  }
}

#endif // SYSEX_HANDLING_H
