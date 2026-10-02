/*
 * Host test for MidiInputGuard.h, run against the real Arduino MIDI Library.
 * No board needed: the library builds on a desktop compiler.
 *
 *   g++ -std=c++11 -I <path to MIDI_Library>/src -I control/midi1to8-firmware \
 *       tests/midi_input_guard_test.cpp -o /tmp/guard_test && /tmp/guard_test
 *
 * Every input is run twice, through the library's own SerialMIDI transport
 * ("stock") and through MidiInputGuard ("guard"), and the callbacks record
 * what comes out:
 *   - lockup scenarios, each followed by a Note On, Note Off and CC that must
 *     come through;
 *   - well-formed input, where the guard must not change anything;
 *   - 20000 random byte streams followed by the same probe notes;
 *   - 20000 random well-formed streams (running status, clock interleaved,
 *     SysEx longer than the library buffer), compared event by event.
 */

#include <MIDI.h>
#include "MidiInputGuard.h"
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>

struct FakeSerial {
  std::vector<byte> in; size_t pos = 0;
  void begin(long) {}
  int available() { return int(in.size() - pos); }
  int read() { return in[pos++]; }
  size_t write(byte) { return 1; }
};

static std::vector<std::string> ev;
static char buf[64];
#define LOG(...) do { snprintf(buf, sizeof buf, __VA_ARGS__); ev.push_back(buf); } while (0)
void on(byte c, byte n, byte v)  { LOG("on %d %d %d", c, n, v); }
void off(byte c, byte n, byte v) { LOG("off %d %d %d", c, n, v); }
void cc(byte c, byte n, byte v)  { LOG("cc %d %d %d", c, n, v); }
void pc(byte c, byte n)          { LOG("pc %d %d", c, n); }
void pb(byte c, int b)           { LOG("pb %d %d", c, b); }
void clk()                       { LOG("clock"); }
void sx(byte* d, unsigned len)   { std::string s = "sysex"; for (unsigned i = 0; i < len; i++) { snprintf(buf, sizeof buf, " %02X", d[i]); s += buf; } ev.push_back(s); }

template <class T> void setup(T& m) {
  m.setHandleNoteOn(on); m.setHandleNoteOff(off); m.setHandleControlChange(cc);
  m.setHandleProgramChange(pc); m.setHandlePitchBend(pb); m.setHandleClock(clk);
  m.setHandleSystemExclusive(sx); m.begin(MIDI_CHANNEL_OMNI); m.turnThruOff();
}

std::vector<std::string> runStock(const std::vector<byte>& in) {
  ev.clear(); FakeSerial s; s.in = in;
  midi::SerialMIDI<FakeSerial> t(s); midi::MidiInterface<midi::SerialMIDI<FakeSerial>> m(t);
  setup(m); while (s.available()) m.read(); return ev;
}
std::vector<std::string> runGuard(const std::vector<byte>& in) {
  ev.clear(); FakeSerial s; s.in = in;
  MidiInputGuard<FakeSerial> t(s); midi::MidiInterface<MidiInputGuard<FakeSerial>> m(t);
  setup(m); while (s.available() || t.available()) m.read(); return ev;
}

static int fails = 0;
bool hasNotes(const std::vector<std::string>& e) {   // the probe notes at the end of each scenario
  int n = 0; for (auto& x : e) if (x == "on 1 60 100" || x == "off 1 60 0" || x == "cc 2 7 64") n++;
  return n == 3;
}
const std::vector<byte> PROBE = {0x90, 60, 100, 0x80, 60, 0, 0xB1, 7, 64};

void scenario(const char* name, std::vector<byte> in, bool expectStockOk) {
  in.insert(in.end(), PROBE.begin(), PROBE.end());
  auto a = runStock(in), b = runGuard(in);
  bool sOk = hasNotes(a), gOk = hasNotes(b);
  printf("%-46s stock: %-6s guard: %s\n", name, sOk ? "notes" : "STUCK", gOk ? "notes" : "STUCK");
  if (!gOk || sOk != expectStockOk) fails++;
}

void same(const char* name, std::vector<byte> in) {   // well-formed input: guard must not change anything
  auto a = runStock(in), b = runGuard(in);
  printf("%-46s %s (%zu events)\n", name, a == b ? "identical" : "DIFFERENT", a.size());
  if (a != b) { fails++; for (size_t i = 0; i < std::max(a.size(), b.size()); i++) printf("   %-24s | %s\n", i < a.size() ? a[i].c_str() : "", i < b.size() ? b[i].c_str() : ""); }
}

int main() {
  printf("-- lockup scenarios, then Note On / Note Off / CC --\n");
  scenario("orphan F0", {0xF0, 0x41, 0x10}, false);
  scenario("orphan F7", {0xF7}, false);
  scenario("F0 inside a Note On", {0x90, 60, 0xF0, 100, 0x91, 61, 1}, true);
  std::vector<byte> longsx = {0xF0}; for (int i = 0; i < 300; i++) longsx.push_back(i & 0x7F);
  scenario("truncated 300-byte SysEx", longsx, false);
  scenario("orphan F0 with clock interleaved", {0xF0, 1, 0xF8, 2, 0xF8}, false);
  scenario("SysEx interrupted by F0 then truncated", {0xF0, 1, 2, 0xF0, 3}, true);

  printf("-- well-formed input --\n");
  same("notes, CC, PC, pitch bend", {0x90, 60, 100, 0x80, 60, 0, 0xB3, 1, 2, 0xC4, 5, 0xE0, 0, 64});
  same("running status", {0x90, 60, 100, 61, 100, 62, 0, 0xB0, 7, 1, 7, 2});
  same("clock inside a Note On", {0x90, 0xF8, 60, 0xF8, 100});
  same("clock inside a SysEx", {0xF0, 0x7D, 0xF8, 0x18, 0xF7, 0x90, 60, 1});
  same("config Ping F0 7D 18 01 01 F7", {0xF0, 0x7D, 0x18, 0x01, 0x01, 0xF7});
  std::vector<byte> wr = {0xF0, 0x7D, 0x18, 0x01, 0x03, 0x00}; for (int i = 0; i < 20; i++) wr.push_back(0x7F); wr.push_back(0xF7);
  same("config Write Preset (27 bytes)", wr);
  std::vector<byte> big = {0xF0}; for (int i = 0; i < 300; i++) big.push_back(i & 0x7F); big.push_back(0xF7); big.push_back(0x90); big.push_back(1); big.push_back(1);
  same("complete 302-byte SysEx (split in chunks)", big);

  printf("-- fuzz: 20000 random streams, then the probe notes --\n");
  srand(1);
  int stockStuck = 0, guardStuck = 0;
  for (int k = 0; k < 20000; k++) {
    std::vector<byte> in; int n = 1 + rand() % 40;
    for (int i = 0; i < n; i++) { int r = rand() % 10; in.push_back(r < 6 ? rand() % 128 : 0x80 + rand() % 128); }
    in.insert(in.end(), PROBE.begin(), PROBE.end());
    if (!hasNotes(runStock(in))) stockStuck++;
    if (!hasNotes(runGuard(in))) { guardStuck++; if (guardStuck < 4) { printf("   guard stuck on:"); for (byte x : in) printf(" %02X", x); printf("\n"); } }
  }
  printf("stock lost the probe notes in %d streams, guard in %d\n", stockStuck, guardStuck);
  if (guardStuck) fails++;
  printf("-- fuzz: 20000 random well-formed streams, stock vs guard --\n");
  int differ = 0;
  for (int k = 0; k < 20000; k++) {
    std::vector<byte> in; byte running = 0;
    int msgs = 1 + rand() % 12;
    for (int j = 0; j < msgs; j++) {
      int kind = rand() % 6;
      if (kind == 5) {                        // complete SysEx, sometimes longer than the 128-byte buffer
        in.push_back(0xF0); int n = rand() % 2 ? rand() % 20 : 100 + rand() % 200;
        for (int i = 0; i < n; i++) { in.push_back(rand() % 128); if (rand() % 30 == 0) in.push_back(0xF8); }
        in.push_back(0xF7); running = 0;
      } else {
        static const byte types[] = {0x80, 0x90, 0xB0, 0xC0, 0xE0};
        byte st = types[kind] | (rand() % 16);
        if (st != running || rand() % 2) in.push_back(st);   // running status half of the time
        running = st;
        int nd = (st & 0xF0) == 0xC0 ? 1 : 2;
        for (int i = 0; i < nd; i++) { if (rand() % 10 == 0) in.push_back(0xF8); in.push_back(rand() % 128); }
      }
    }
    if (runStock(in) != runGuard(in)) differ++;
  }
  printf("guard output differs from stock in %d streams\n", differ);
  if (differ) fails++;

  printf(fails ? "\nFAILED: %d\n" : "\nALL PASS\n", fails);
  return fails != 0;
}
