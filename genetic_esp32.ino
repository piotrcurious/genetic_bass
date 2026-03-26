// ESP32 funky house bassline generator using DAC
// BPM_PIN controls BPM (analog)
// Bassline is generated for 64 steps
// GPIO25 outputs 8-bit audio signal using DAC

#include <Arduino.h>
#include <driver/dac.h>
#include <cmath>

#define BEAT_PIN 2
#define LIKE_PIN 4
#define DISLIKE_PIN 5
#define BPM_PIN 36

const int NUM_STEPS = 64;
const int NUM_NOTES = 12;
const int POP_SIZE = 100;
const float MUT_RATE = 0.05;

struct Genome {
  byte note[NUM_STEPS];
  byte octave[NUM_STEPS];
  byte waveform[NUM_STEPS];
  byte gate[NUM_STEPS];
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

const byte CHORD_PROGRESSION[4][2] = {
  {0,0}, {1,9}, {2,7}, {1,2}
};

int freq_lut[128];
byte sine_lut[256];

void initLUTs() {
  for (int i = 0; i < 128; i++) freq_lut[i] = (int)(440.0 * pow(2.0, (i - 69) / 12.0));
  for (int i = 0; i < 256; i++) sine_lut[i] = (byte)(128 + 127 * sin(i * 2.0 * M_PI / 256.0));
}

Genome population[POP_SIZE];
Genome best_genome;
int current_step = 0;
int current_bpm;
unsigned long step_time;
unsigned long last_time;
bool beat_on = false;

int note_bias[NUM_NOTES] = {0};
int octave_bias[3] = {0};

// Audio variables
volatile uint32_t phase = 0;
volatile uint32_t phase_inc = 0;
volatile byte current_waveform = 1;
volatile uint16_t amplitude = 0; // 0-255 (fixed point 8.8)
volatile uint16_t decay_rate = 50; // amount to subtract per sample

hw_timer_t * timer = NULL;
const int SAMPLE_RATE = 20000;

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
    // Corrected fixed-point scaling: (raw_sample * amp_val) >> 8
    int amp_val = amplitude >> 8;
    int out = 128 + ((raw_sample * amp_val) >> 8);
    dac_output_voltage(DAC_CHANNEL_1, (byte)out);

    if (amplitude > decay_rate) amplitude -= decay_rate;
    else {
      amplitude = 0;
      phase_inc = 0;
    }
  } else {
    dac_output_voltage(DAC_CHANNEL_1, 128);
    phase_inc = 0;
  }
}

int mtof(byte note, byte octave) {
  int midi = note + octave * 12 + 24;
  if (midi > 127) midi = 127;
  return freq_lut[midi];
}

int evaluateGenome(const Genome& genome) {
  int score = 0;
  for (int i = 0; i < NUM_STEPS; i++) {
    byte note = genome.note[i];
    byte octave = genome.octave[i];
    byte chord_type = CHORD_PROGRESSION[i / (NUM_STEPS / 4)][0];
    byte chord_root = CHORD_PROGRESSION[i / (NUM_STEPS / 4)][1];
    byte scale_degree = (note - chord_root + NUM_NOTES) % NUM_NOTES;
    score += JAZZ_TABLE[chord_type][scale_degree];
    score -= abs(octave - 1) * 10;
    score += note_bias[note] * 5;
    score += octave_bias[octave] * 5;
    if (i % 4 == 0) {
        if (note == chord_root) score += 20;
        if (genome.gate[i] == 0) score -= 10;
    }
    if (i > 0) {
      int interval = (note + octave * 12) - (genome.note[i-1] + genome.octave[i-1] * 12);
      score -= abs(interval) * 3;
    }
  }
  return score;
}

void evaluatePopulation() {
  int best_score = -999999;
  int current_best_idx = 0;
  for (int i = 0; i < POP_SIZE; i++) {
    int score = evaluateGenome(population[i]);
    if (score > best_score) {
      best_score = score;
      current_best_idx = i;
    }
  }
  best_genome = population[current_best_idx];
}

void initPopulation() {
  for (int i = 0; i < POP_SIZE; i++) {
    for (int j = 0; j < NUM_STEPS; j++) {
      population[i].note[j] = random(NUM_NOTES);
      population[i].octave[j] = random(3);
      population[i].waveform[j] = random(4);
      population[i].gate[j] = (random(10) < 7) ? 1 : 0;
    }
  }
}

// next_gen declared globally (on heap/data segment) to avoid stack overflow
Genome next_gen[POP_SIZE];

void mutatePopulation() {
  next_gen[0] = best_genome;
  for (int i = 1; i < POP_SIZE; i++) {
    Genome child;
    Genome parent2 = population[random(POP_SIZE)];
    for(int j=0; j<NUM_STEPS; j++) {
      child.note[j] = (random(100) < 50) ? best_genome.note[j] : parent2.note[j];
      child.octave[j] = (random(100) < 50) ? best_genome.octave[j] : parent2.octave[j];
      child.gate[j] = (random(100) < 50) ? best_genome.gate[j] : parent2.gate[j];
      child.waveform[j] = best_genome.waveform[j];
      if ((float)random(100)/100.0 < MUT_RATE) child.note[j] = random(NUM_NOTES);
      if ((float)random(100)/100.0 < MUT_RATE) child.octave[j] = random(3);
      if ((float)random(100)/100.0 < MUT_RATE) child.gate[j] = (random(10) < 7) ? 1 : 0;
    }
    next_gen[i] = child;
  }
  for(int i=0; i<POP_SIZE; i++) population[i] = next_gen[i];
}

void updateBias() {
  for(int i=0; i<NUM_STEPS; i++) {
    if (best_genome.gate[i]) {
      note_bias[best_genome.note[i]]++;
      octave_bias[best_genome.octave[i]]++;
    }
  }
}

void playStep() {
  byte note = best_genome.note[current_step];
  byte octave = best_genome.octave[current_step];
  byte gate = best_genome.gate[current_step];
  byte waveform = best_genome.waveform[current_step];
  int freq = mtof(note, octave);
  if (gate == 1) {
    current_waveform = waveform;
    phase_inc = (uint32_t)(((double)freq / SAMPLE_RATE) * 4294967296.0);
    amplitude = 65535; // Max amplitude (255.255)
  }
  Serial.print(gate ? freq : 0);
  Serial.print(current_step == NUM_STEPS - 1 ? "\n" : " ");
}

void setup() {
  Serial.begin(115200);
  initLUTs();
  pinMode(BEAT_PIN, OUTPUT);
  pinMode(LIKE_PIN, INPUT_PULLUP);
  pinMode(DISLIKE_PIN, INPUT_PULLUP);
  dac_enable_voltage_output(DAC_CHANNEL_1);

  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 1000000 / SAMPLE_RATE, true);
  timerAlarmEnable(timer);

  initPopulation();
  evaluatePopulation();
  current_bpm = analogRead(BPM_PIN) / 16 + 60;
  step_time = 60000 / current_bpm / 4;
  last_time = millis();
}

void loop() {
  unsigned long current_time = millis();
  if (current_time - last_time >= step_time) {
    last_time = current_time;
    playStep();
    current_step++;
    if (current_step == NUM_STEPS) {
      current_step = 0;
      if (digitalRead(LIKE_PIN) == LOW) {
        updateBias();
        mutatePopulation();
        evaluatePopulation();
      } else if (digitalRead(DISLIKE_PIN) == LOW) {
        initPopulation();
        for(int i=0; i<NUM_NOTES; i++) note_bias[i] = 0;
        for(int i=0; i<3; i++) octave_bias[i] = 0;
        evaluatePopulation();
      }
    }
    beat_on = !beat_on;
    digitalWrite(BEAT_PIN, beat_on ? HIGH : LOW);
    current_bpm = analogRead(BPM_PIN) / 16 + 60;
    step_time = 60000 / current_bpm / 4;
    decay_rate = 65535 / (SAMPLE_RATE * step_time / 1000);
  }
}
