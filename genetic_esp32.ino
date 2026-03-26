// ESP32 funky house bassline generator using DAC
// BPM_PIN controls BPM (analog)
// Bassline is generated for 64 steps
// GPIO25 outputs 8-bit audio signal using DAC
// Bassline generator algorithm uses jazz music theory convergence table
// Evolutionary algorithm converges based on user "like" and "dislike" buttons

#include <Arduino.h>
#include <driver/dac.h>
#include <cmath>

#define BEAT_PIN 2 // digital pin for beat trigger signal
#define LIKE_PIN 4 // digital pin for like button
#define DISLIKE_PIN 5 // digital pin for dislike button
#define BPM_PIN 36 // analog pin for BPM control (VP)

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
    {10, -5, 2, -5, 8, 5, -5, 10, -5, 5, -5, 8}, // Major
    {10, -5, 5, 10, -5, 5, -5, 10, -5, 2, 8, -5}, // Minor
    {10, -5, 2, -5, 8, 5, -5, 10, -5, 5, 8, -5}, // Dominant
    {10, -5, -5, 10, -5, -5, 10, -5, -5, 10, -5, -5}, // Diminished
    {10, -5, -5, 10, -5, -5, 8, -5, -5, 2, 8, -5}, // Half-dim
    {10, -5, -5, -5, 10, -5, -5, 8, -5, -5, -5, -5}, // Augmented
    {10, -5, -5, -5, -5, 10, 8, 10, -5, -5, -5, 8}  // Suspended
};

const byte CHORD_PROGRESSION[4][2] = {
  {0,0}, {1,9}, {2,7}, {1,2}
};

Genome population[POP_SIZE];
Genome best_genome;
int current_step = 0;
int current_bpm;
unsigned long step_time;
unsigned long last_time;
bool beat_on = false;

// Audio variables
volatile float phase = 0;
volatile float phase_inc = 0;
volatile int current_sample = 128;
volatile byte current_waveform = 1;

hw_timer_t * timer = NULL;
const int SAMPLE_RATE = 20000;

void IRAM_ATTR onTimer() {
  if (phase_inc > 0) {
    phase += phase_inc;
    if (phase >= 1.0) phase -= 1.0;

    switch(current_waveform) {
      case 0: // Sine
        current_sample = (int)(128 + 127 * sin(phase * 2.0 * M_PI));
        break;
      case 1: // Saw
        current_sample = (int)(phase * 255);
        break;
      case 2: // Triangle
        current_sample = (int)(phase < 0.5 ? phase * 510 : (1.0 - phase) * 510);
        break;
      case 3: // Square
        current_sample = (phase < 0.5 ? 255 : 0);
        break;
      default:
        current_sample = (int)(phase * 255);
        break;
    }
    dac_output_voltage(DAC_CHANNEL_1, (byte)current_sample); // GPIO25
  } else {
    dac_output_voltage(DAC_CHANNEL_1, 0);
  }
}

int mtof(byte note, byte octave) {
  return (int)(440.0 * pow(2.0, (note + octave * 12 + 24 - 69) / 12.0));
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
    if (i % 4 == 0) {
        if (note == chord_root) score += 20;
        if (genome.gate[i] == 0) score -= 10;
    }
    if (i > 0) {
      int interval = (note + octave * 12) - (genome.note[i-1] + genome.octave[i-1] * 12);
      score -= abs(interval) * 3;
      if (interval == 0 && genome.gate[i] == 1 && genome.gate[i-1] == 1) {
        score -= 15;
      }
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

void mutatePopulation() {
  // Elitism: keep best_genome in the next population
  Genome next_gen[POP_SIZE];
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

void playStep() {
  byte note = best_genome.note[current_step];
  byte octave = best_genome.octave[current_step];
  byte gate = best_genome.gate[current_step];
  byte waveform = best_genome.waveform[current_step];

  int freq = mtof(note, octave);

  if (gate == 1) {
    current_waveform = waveform;
    phase_inc = (float)freq / SAMPLE_RATE;
  } else {
    phase_inc = 0;
  }

  // Output frequency to serial for analysis
  Serial.print(gate ? freq : 0);
  Serial.print(current_step == NUM_STEPS - 1 ? "\n" : " ");
}

void setup() {
  Serial.begin(115200);
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
        mutatePopulation();
        evaluatePopulation();
      } else if (digitalRead(DISLIKE_PIN) == LOW) {
        initPopulation();
        evaluatePopulation();
      }
    }

    beat_on = !beat_on;
    digitalWrite(BEAT_PIN, beat_on ? HIGH : LOW);
    current_bpm = analogRead(BPM_PIN) / 16 + 60;
    step_time = 60000 / current_bpm / 4;
  }
}
