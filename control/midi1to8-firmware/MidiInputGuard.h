/*
 * MidiInputGuard - serial transport for the Arduino MIDI Library 5.x that
 * keeps the library's parser from getting stuck in SysEx state.
 *
 * The problem: once the library's parser is in SysEx state, the only way out
 * is an F7. Every other status byte, a Note On included, is stored as SysEx
 * data, and when the buffer fills the parser re-enters SysEx state. Real-time
 * bytes bypass the parser, so the module keeps passing clock while every note
 * and CC is dropped until power cycle. A single F0 that never gets its F7 is
 * enough, and so is a lone F7: at the start of a message the library treats an
 * F7 like an F0. See FortySevenEffects/arduino_midi_library#367.
 *
 * The fix: this transport sits between Serial and the library and only lets
 * well-formed input through.
 *   - Real-time bytes (F8-FF) pass immediately, as before.
 *   - Channel and System Common messages are held until complete and then
 *     passed in one go, always with their status byte. A message cut short
 *     by another status byte is dropped, as the MIDI spec requires.
 *   - SysEx data passes through as it arrives. If any status byte other than
 *     real-time arrives before the F7, an F7 is inserted to close the SysEx
 *     and the new message is then handled normally.
 *   - An F7 outside a SysEx, a data byte with no status, and the undefined
 *     F4/F5 are dropped.
 * So the library only sees a status byte when it is between messages, and
 * cannot get stuck whatever arrives on the wire.
 *
 * A SysEx closed by an inserted F7 reaches the SysEx handler looking complete,
 * so the handler still has to check the length of what it receives.
 *
 * Usage, instead of MIDI_CREATE_DEFAULT_INSTANCE():
 *
 *   MidiInputGuard<HardwareSerial> guardedSerial(Serial);
 *   midi::MidiInterface<MidiInputGuard<HardwareSerial>> MIDI(guardedSerial);
 */

#ifndef MIDI_INPUT_GUARD_H
#define MIDI_INPUT_GUARD_H

#include <MIDI.h>

template <class SerialPort>
class MidiInputGuard
{
public:
  MidiInputGuard(SerialPort& inSerial)
    : mSerial(inSerial)
  {
  }

  // Same interface as the library's own SerialMIDI transport
  static const bool thruActivated = true;

  void begin()
  {
    mSerial.begin(31250);
  }

  bool beginTransmission(midi::MidiType)
  {
    return true;
  }

  void write(byte value)
  {
    mSerial.write(value);
  }

  void endTransmission()
  {
  }

  unsigned available()
  {
    // Pull bytes from the UART until something is ready for the library
    while (mOutPos == mOutLen && mSerial.available()) {
      mOutPos = mOutLen = 0;
      process(mSerial.read());
    }
    return mOutLen - mOutPos;
  }

  byte read()
  {
    return mOut[mOutPos++];   // the library only calls read() after available()
  }

private:
  // Number of data bytes that follow a status byte
  static byte dataLength(byte status)
  {
    switch (status & 0xF0) {
      case 0xC0:              // Program Change
      case 0xD0:              // Channel Pressure
        return 1;
      case 0xF0:              // System Common
        return (status == 0xF2) ? 2 : (status == 0xF1 || status == 0xF3) ? 1 : 0;
      default:                // Note Off/On, Poly Pressure, CC, Pitch Bend
        return 2;
    }
  }

  void emit(byte b)
  {
    mOut[mOutLen++] = b;
  }

  void process(byte b)
  {
    if (b >= 0xF8) {          // real-time: never part of another message
      emit(b);
      return;
    }

    if (b >= 0x80) {          // status byte
      mPendingLen = 0;        // drops any incomplete message
      if (mInSysEx) {
        mInSysEx = false;
        emit(0xF7);           // the real EOX, or one standing in for it
        if (b == 0xF7) {
          return;
        }
      } else if (b == 0xF7) {
        return;               // EOX with no SysEx open
      }

      if (b == 0xF0) {
        mInSysEx = true;
        mRunningStatus = 0;
        emit(b);
      } else if (b == 0xF4 || b == 0xF5) {
        mRunningStatus = 0;   // undefined
      } else if (dataLength(b) == 0) {
        mRunningStatus = 0;   // Tune Request
        emit(b);
      } else {
        mPending[0] = b;
        mPendingLen = 1;
        mRunningStatus = (b < 0xF0) ? b : 0;  // System Common cancels it
      }
      return;
    }

    // data byte
    if (mInSysEx) {
      emit(b);
      return;
    }
    if (mPendingLen == 0) {
      if (mRunningStatus == 0) {
        return;               // no status to attach it to
      }
      mPending[0] = mRunningStatus;
      mPendingLen = 1;
    }
    mPending[mPendingLen++] = b;
    if (mPendingLen == 1 + dataLength(mPending[0])) {
      for (byte i = 0; i < mPendingLen; i++) {
        emit(mPending[i]);
      }
      mPendingLen = 0;
    }
  }

  SerialPort& mSerial;

  byte mPending[3];           // channel or System Common message being received
  byte mPendingLen = 0;
  byte mRunningStatus = 0;    // 0 = none
  bool mInSysEx = false;

  byte mOut[3];               // bytes ready for the library
  byte mOutLen = 0;
  byte mOutPos = 0;
};

#endif
