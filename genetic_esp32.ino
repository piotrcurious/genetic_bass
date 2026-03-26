// ESP32 production-level funky house bassline generator using DAC
#include <Arduino.h>
#include <driver/dac.h>
#include <cmath>
#include <LittleFS.h>

// --- Configuration & Constants ---
#define BEAT_PIN 2
#define LIKE_PIN 4
#define DISLIKE_PIN 5
#define BPM_PIN 36

const int NUM_STEPS = 64;
const int NUM_NOTES = 12;
const int POP_SIZE = 100;
const float MUT_RATE = 0.05;
const int SAMPLE_RATE = 20000;

struct Genome {
  byte note[NUM_STEPS];
  byte octave[NUM_STEPS];
  byte waveform[NUM_STEPS];
  byte gate[NUM_STEPS];
  byte tie[NUM_STEPS];
};

enum Progression {
  HOUSE, BLUES, JAZZ_II_V_I, FUNK, POP, MINOR_BLUES, ROCK,
  TECHNO, PSYTRANCE, TRAP, REGGAE, BOSSA_NOVA, SYNTHWAVE, DISCO, METAL, AMBIENT
};
const char* PROG_NAMES[] = {
  "HOUSE", "BLUES", "JAZZ II-V-I", "FUNK", "POP", "MINOR BLUES", "ROCK",
  "TECHNO", "PSYTRANCE", "TRAP", "REGGAE", "BOSSA NOVA", "SYNTHWAVE", "DISCO", "METAL", "AMBIENT"
};

const int JAZZ_TABLE[7][12] = {
    {10, -5, 2, -5, 8, 5, -5, 10, -5, 5, -5, 8},
    {10, -5, 5, 10, -5, 5, -5, 10, -5, 2, 8, -5},
    {10, -5, 2, -5, 8, 5, -5, 10, -5, 5, 8, -5},
    {10, -5, -5, 10, -5, -5, 10, -5, -5, 10, -5, -5},
    {10, -5, -5, 10, -5, -5, 8, -5, -5, 2, 8, -5},
    {10, -5, -5, -5, 10, -5, -5, 8, -5, -5, -5, -5},
    {10, -5, -5, -5, -5, 10, 8, 10, -5, -5, -5, 8}
};

const byte PROGRESSIONS[16][4][2] = {
  {{0,0}, {1,9}, {2,7}, {1,2}}, // House
  {{2,0}, {2,5}, {2,0}, {2,7}}, // Blues
  {{1,2}, {2,7}, {0,0}, {0,0}}, // Jazz II-V-I
  {{1,4}, {1,4}, {2,9}, {2,9}}, // Funk
  {{0,0}, {0,7}, {1,9}, {0,5}}, // Pop
  {{1,0}, {1,5}, {1,0}, {1,7}}, // Minor Blues
  {{0,2}, {0,0}, {0,7}, {0,5}}, // Rock
  {{1,9}, {1,9}, {1,9}, {1,9}}, // Techno
  {{1,2}, {1,2}, {1,2}, {1,2}}, // Psytrance
  {{1,0}, {1,1}, {1,0}, {1,1}}, // Trap
  {{1,7}, {0,5}, {1,7}, {2,2}}, // Reggae
  {{0,0}, {2,7}, {1,2}, {2,7}}, // Bossa
  {{1,4}, {1,0}, {0,7}, {0,5}}, // Synthwave
  {{1,9}, {0,5}, {0,0}, {0,7}}, // Disco
  {{1,4}, {1,5}, {1,4}, {1,1}}, // Metal
  {{0,0}, {0,5}, {0,10}, {0,5}} // Ambient
};

// --- Global State ---
Genome population[POP_SIZE];
Genome next_gen[POP_SIZE];
Genome best_genome;
Progression current_prog = HOUSE;
int note_bias[NUM_NOTES] = {0};
int octave_bias[3] = {0};
int current_step = 0;
unsigned long step_time;
unsigned long last_time;
bool beat_on = false;

// Button state
bool last_like = HIGH;
bool last_dislike = HIGH;
unsigned long like_time = 0;
unsigned long dislike_time = 0;
const unsigned long DEBOUNCE = 50;

// Audio state
volatile uint32_t phase = 0;
volatile uint32_t phase_inc = 0;
volatile byte current_waveform = 1;
volatile uint32_t amplitude = 0;
volatile uint32_t decay_rate = 100;
int freq_lut[128];
byte sine_lut[256];
hw_timer_t * timer = NULL;

// --- Flash Storage ---
void saveState() {
  File f = LittleFS.open("/state.bin", "w");
  if (!f) return;
  f.write((uint8_t*)&best_genome, sizeof(best_genome));
  f.write((uint8_t*)note_bias, sizeof(note_bias));
  f.write((uint8_t*)octave_bias, sizeof(octave_bias));
  f.write((uint8_t*)&current_prog, sizeof(current_prog));
  f.close();
  Serial.println("State saved.");
}

void loadState() {
  if (!LittleFS.exists("/state.bin")) return;
  File f = LittleFS.open("/state.bin", "r");
  if (!f) return;
  f.read((uint8_t*)&best_genome, sizeof(best_genome));
  f.read((uint8_t*)note_bias, sizeof(note_bias));
  f.read((uint8_t*)octave_bias, sizeof(octave_bias));
  f.read((uint8_t*)&current_prog, sizeof(current_prog));
  f.close();
  population[0] = best_genome;
  Serial.println("State loaded.");
}

// --- Audio Synthesis ---
void IRAM_ATTR onTimer() {
  if (amplitude > 0) {
    phase += phase_inc;
    byte idx = (phase >> 24) & 0xFF;
    int raw_sample = 0;
    switch(current_waveform) {
      case 0: raw_sample = (int)sine_lut[idx] - 128; break;
      case 1: raw_sample = (int)idx - 128; break;
      case 2: raw_sample = (int)(idx < 128 ? idx * 2 : (255 - idx) * 2) - 128; break;
      case 3: raw_sample = (idx < 128 ? 127 : -128); break;
    }
    uint32_t amp_val = amplitude >> 16;
    int out = 128 + ((raw_sample * (int)amp_val) >> 16);
    dac_output_voltage(DAC_CHANNEL_1, (byte)out);
    if (amplitude > decay_rate) amplitude -= decay_rate;
    else { amplitude = 0; phase_inc = 0; }
  } else { dac_output_voltage(DAC_CHANNEL_1, 128); phase_inc = 0; }
}

void initAudio() {
  for (int i = 0; i < 128; i++) freq_lut[i] = (int)(440.0 * pow(2.0, (i - 69) / 12.0));
  for (int i = 0; i < 256; i++) sine_lut[i] = (byte)(128 + 127 * sin(i * 2.0 * M_PI / 256.0));
  dac_enable_voltage_output(DAC_CHANNEL_1);
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 1000000 / SAMPLE_RATE, true);
  timerAlarmEnable(timer);
}

// --- Genetic Algorithm ---
int evaluateGenome(const Genome& genome) {
  int score = 0;
  for (int i = 0; i < NUM_STEPS; i++) {
    byte chord_type = PROGRESSIONS[current_prog][i / (NUM_STEPS / 4)][0];
    byte chord_root = PROGRESSIONS[current_prog][i / (NUM_STEPS / 4)][1];
    byte scale_degree = (genome.note[i] - chord_root + NUM_NOTES) % NUM_NOTES;
    score += JAZZ_TABLE[chord_type][scale_degree];
    score -= abs(genome.octave[i] - 1) * 10;
    score += note_bias[genome.note[i]] * 5;
    score += octave_bias[genome.octave[i]] * 5;

    // --- Style-Dependent Rhythmic Scoring ---
    if (i % 4 == 0) { // Strong beats
        if (genome.note[i] == chord_root) score += 20;
        if (genome.gate[i] == 0) score -= 15; // Prefer notes on downbeats
        if (current_prog == TECHNO || current_prog == PSYTRANCE) {
           if (genome.gate[i] == 1) score += 10; // Extra reward for techno downbeat
        }
    }

    if (current_prog == REGGAE) {
       if (i % 4 == 0) score -= 20; // Penalize downbeat notes for reggae feel
       if (i % 4 == 2) { if (genome.gate[i]) score += 25; } // Reward offbeats
    }

    if (i > 0) {
      int interval = (genome.note[i] + genome.octave[i] * 12) - (genome.note[i-1] + genome.octave[i-1] * 12);
      score -= abs(interval) * 3;
      if (interval == 0 && genome.gate[i] == 1 && genome.gate[i-1] == 1 && genome.tie[i-1] == 0) score -= 15;
      if (genome.tie[i-1] == 1 && (genome.gate[i] == 0 || interval != 0)) score -= 20;
      if (genome.tie[i-1] == 1 && interval == 0 && genome.gate[i] == 1) score += 10;
    }
  }
  return score;
}

void evaluatePopulation() {
  int best_score = -999999;
  int best_idx = 0;
  for (int i = 0; i < POP_SIZE; i++) {
    int score = evaluateGenome(population[i]);
    if (score > best_score) { best_score = score; best_idx = i; }
  }
  best_genome = population[best_idx];
}

void initPopulation() {
  for (int i = 0; i < POP_SIZE; i++) {
    for (int j = 0; j < NUM_STEPS; j++) {
      population[i].note[j] = random(NUM_NOTES);
      population[i].octave[j] = random(3);
      population[i].waveform[j] = random(4);
      population[i].gate[j] = (random(10) < 7) ? 1 : 0;
      population[i].tie[j] = (random(10) < 2) ? 1 : 0;
    }
  }
}

void mutatePopulation(float rate = 0.05) {
  next_gen[0] = best_genome;
  for (int i = 1; i < POP_SIZE; i++) {
    Genome p2 = population[random(POP_SIZE)];
    int cp1 = random(NUM_STEPS), cp2 = random(NUM_STEPS);
    if (cp1 > cp2) std::swap(cp1, cp2);
    for(int j=0; j<NUM_STEPS; j++) {
      bool from_p1 = (j < cp1 || j > cp2);
      next_gen[i].note[j] = from_p1 ? best_genome.note[j] : p2.note[j];
      next_gen[i].octave[j] = from_p1 ? best_genome.octave[j] : p2.octave[j];
      next_gen[i].gate[j] = from_p1 ? best_genome.gate[j] : p2.gate[j];
      next_gen[i].tie[j] = from_p1 ? best_genome.tie[j] : p2.tie[j];
      next_gen[i].waveform[j] = best_genome.waveform[j];
      if ((float)random(100)/100.0 < rate) {
        next_gen[i].note[j] = random(NUM_NOTES);
        next_gen[i].octave[j] = random(3);
        next_gen[i].gate[j] = (random(10) < 7) ? 1 : 0;
        next_gen[i].tie[j] = (random(10) < 2) ? 1 : 0;
      }
    }
  }
  for(int i=0; i<POP_SIZE; i++) population[i] = next_gen[i];
}

// --- Commands ---
void processSerial() {
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();
    if (cmd == "save") saveState();
    else if (cmd == "load") { loadState(); evaluatePopulation(); }
    else if (cmd == "reset") { initPopulation(); for(int i=0; i<NUM_NOTES; i++) note_bias[i]=0; for(int i=0; i<3; i++) octave_bias[i]=0; evaluatePopulation(); Serial.println("Reset."); }
    else if (cmd == "list") { for(int i=0; i<16; i++) { Serial.print(i); Serial.print(": "); Serial.println(PROG_NAMES[i]); } }
    else if (cmd == "panic") { amplitude = 0; phase_inc = 0; Serial.println("Panic!"); }
    else if (cmd.startsWith("prog ")) { int p = cmd.substring(5).toInt(); current_prog = (Progression)(p % 16); initPopulation(); evaluatePopulation(); Serial.print("Prog: "); Serial.println(PROG_NAMES[current_prog]); }
    else Serial.println("Commands: save, load, reset, list, panic, prog [0-15]");
  }
}

// --- Sequencer & UI ---
void playStep() {
  byte note = best_genome.note[current_step];
  byte octave = best_genome.octave[current_step];
  int freq = freq_lut[note + octave * 12 + 24];
  if (best_genome.gate[current_step] == 1) {
    bool is_tie = false;
    int prev = (current_step == 0) ? NUM_STEPS - 1 : current_step - 1;
    if (best_genome.tie[prev] == 1 && best_genome.note[current_step] == best_genome.note[prev] && best_genome.octave[current_step] == best_genome.octave[prev]) is_tie = true;
    current_waveform = best_genome.waveform[current_step];
    phase_inc = (uint32_t)(((double)freq / SAMPLE_RATE) * 4294967296.0);
    if (!is_tie) amplitude = 0xFFFFFFFF;
    Serial.print(is_tie ? "T" : ""); Serial.print(freq);
  } else { Serial.print(0); }
  Serial.print(current_step == NUM_STEPS - 1 ? "\n" : " ");
}

void updateUI() {
  bool like = digitalRead(LIKE_PIN), dislike = digitalRead(DISLIKE_PIN);
  if (like == LOW && dislike == LOW) {
    current_prog = (Progression)((current_prog + 1) % 16);
    initPopulation(); evaluatePopulation();
    Serial.print("\nProg: "); Serial.println(PROG_NAMES[current_prog]);
    delay(500);
  } else if (like == LOW && last_like == HIGH && (millis() - like_time > DEBOUNCE)) {
    for(int i=0; i<NUM_STEPS; i++) if (best_genome.gate[i]) { note_bias[best_genome.note[i]]++; octave_bias[best_genome.octave[i]]++; }
    for(int g=0; g<10; g++) mutatePopulation(0.15);
    evaluatePopulation();
    Serial.println("\nLiked");
    like_time = millis();
  } else if (dislike == LOW && last_dislike == HIGH && (millis() - dislike_time > DEBOUNCE)) {
    initPopulation();
    for(int i=0; i<NUM_NOTES; i++) note_bias[i] = 0;
    for(int i=0; i<3; i++) octave_bias[i] = 0;
    evaluatePopulation();
    Serial.println("\nDisliked");
    dislike_time = millis();
  }
  last_like = like; last_dislike = dislike;
}

void setup() {
  Serial.begin(115200);
  LittleFS.begin(true);
  pinMode(BEAT_PIN, OUTPUT); pinMode(LIKE_PIN, INPUT_PULLUP); pinMode(DISLIKE_PIN, INPUT_PULLUP); pinMode(BPM_PIN, INPUT);
  initAudio(); initPopulation(); loadState(); evaluatePopulation();
  last_time = millis();
}

void loop() {
  processSerial();
  int bpm = analogRead(BPM_PIN) / 16 + 60;
  step_time = 60000 / bpm / 4;
  unsigned long current_time = millis();
  if (current_time - last_time >= step_time) {
    last_time = current_time; playStep(); current_step++;
    if (current_step == NUM_STEPS) current_step = 0;
    beat_on = !beat_on; digitalWrite(BEAT_PIN, beat_on ? HIGH : LOW);
    decay_rate = 0xFFFFFFFF / (SAMPLE_RATE * (step_time * 1.5) / 1000);
  }
  updateUI();
}
