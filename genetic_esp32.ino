// ESP32 production-level funky house bassline generator using DAC and FreeRTOS
#include <Arduino.h>
#include <driver/dac.h>
#include <cmath>
#include <LittleFS.h>

// --- Configuration ---
#define BEAT_PIN 2
#define LIKE_PIN 4
#define DISLIKE_PIN 5
#define BPM_PIN 36
#define LIKE_LED 18
#define DISLIKE_LED 19

const int NUM_STEPS = 64;
const int NUM_NOTES = 12;
const int POP_SIZE = 100;
const int SAMPLE_RATE = 20000;

struct Genome {
  byte note[NUM_STEPS];
  byte octave[NUM_STEPS];
  byte waveform[NUM_STEPS];
  byte gate[NUM_STEPS];
  byte tie[NUM_STEPS];
  byte slide[NUM_STEPS];
};

struct Scale { const char* name; uint16_t mask; };
const Scale SCALES[] = {
  {"Major", 0b101010110101}, {"Minor", 0b010110101101}, {"Dorian", 0b011010101101}, {"Phrygian", 0b010110101011},
  {"Lydian", 0b101010100101}, {"Mixolydian", 0b011010110101}, {"Locrian", 0b010101101011}, {"Pentatonic Maj", 0b101000110101},
  {"Pentatonic Min", 0b010100101001}, {"Blues", 0b010101101001}, {"Chromatic", 0b111111111111}
};

struct Chord { byte root = 0; byte scale_idx = 0; };
struct HarmonicState {
  Chord sequence[8];
  byte sequence_len = 4;
  float groove_density = 0.6;
  float syncopation_bias = 0.5;
  float swing = 0.0;
};

// --- Global State ---
HarmonicState h_state;
Genome population[POP_SIZE], next_gen[POP_SIZE], best_genome;
int note_bias[NUM_NOTES] = {0}, current_step = 0;
bool beat_on = false;
SemaphoreHandle_t genome_mutex;

// Button state
bool last_like = HIGH, last_dislike = HIGH;
unsigned long like_time = 0, dislike_time = 0;
const unsigned long DEBOUNCE = 50;

// Audio state (volatile for ISR)
volatile uint32_t phase = 0, phase_inc = 0, target_phase_inc = 0;
volatile byte current_waveform = 1;
volatile uint32_t amplitude = 0, decay_rate = 100;
volatile int lpf_alpha = 256, lpf_prev = 128;
int freq_lut[128];
byte sine_lut[256];
hw_timer_t * timer = NULL;

// --- Task Handles ---
TaskHandle_t SequencerTask;

// --- Flash Storage ---
void saveState() {
  File f = LittleFS.open("/v3.bin", "w");
  if (!f) return;
  xSemaphoreTake(genome_mutex, portMAX_DELAY);
  f.write((uint8_t*)&best_genome, sizeof(best_genome));
  f.write((uint8_t*)note_bias, sizeof(note_bias));
  f.write((uint8_t*)&h_state, sizeof(h_state));
  xSemaphoreGive(genome_mutex);
  f.close();
  Serial.println("Saved.");
}

void loadState() {
  if (!LittleFS.exists("/v3.bin")) return;
  File f = LittleFS.open("/v3.bin", "r");
  if (!f) return;
  xSemaphoreTake(genome_mutex, portMAX_DELAY);
  f.read((uint8_t*)&best_genome, sizeof(best_genome));
  f.read((uint8_t*)note_bias, sizeof(note_bias));
  f.read((uint8_t*)&h_state, sizeof(h_state));
  population[0] = best_genome;
  xSemaphoreGive(genome_mutex);
  f.close();
  Serial.println("Loaded.");
}

// --- Audio Synthesis (Timer ISR) ---
void IRAM_ATTR onTimer() {
  if (amplitude > 0) {
    if (phase_inc != target_phase_inc) {
      if (abs((int)phase_inc - (int)target_phase_inc) < 1000000) phase_inc = target_phase_inc;
      else if (phase_inc < target_phase_inc) phase_inc += 500000;
      else phase_inc -= 500000;
    }
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
    int out_raw = 128 + ((raw_sample * (int)amp_val) >> 16);
    int out = lpf_prev + ((lpf_alpha * (out_raw - lpf_prev)) >> 8);
    lpf_prev = out;
    dac_output_voltage(DAC_CHANNEL_1, (byte)out);
    if (amplitude > decay_rate) amplitude -= decay_rate;
    else { amplitude = 0; phase_inc = 0; target_phase_inc = 0; }
  } else { dac_output_voltage(DAC_CHANNEL_1, 128); phase_inc = 0; target_phase_inc = 0; lpf_prev = 128; }
}

void initAudio() {
  for (int i = 0; i < 128; i++) freq_lut[i] = (int)(440.0 * pow(2.0, (i - 69) / 12.0));
  for (int i = 0; i < 256; i++) sine_lut[i] = (byte)(128 + 127 * sin(i * 2.0 * M_PI / 256.0));
  dac_output_enable(DAC_CHANNEL_1);
  timer = timerBegin(1000000);
  timerAttachInterrupt(timer, &onTimer);
  timerAlarm(timer, 1000000 / SAMPLE_RATE, true, 0);
}

// --- GA Engine ---
int evaluateGenome(const Genome& genome) {
  int score = 0;
  for (int i = 0; i < NUM_STEPS; i++) {
    int ci = (i * h_state.sequence_len) / NUM_STEPS;
    Chord c = h_state.sequence[ci];
    uint16_t mask = SCALES[c.scale_idx].mask;
    byte rel = (genome.note[i] - c.root + 12) % 12;
    score += ((mask >> rel) & 1) ? 15 : -35;
    if (rel == 0) score += 25; else if (rel == 7) score += 10;
    score += note_bias[genome.note[i]] * 5;
    if (i % 4 == 0) { if (genome.gate[i] == 1) score += 10; else score -= 15; }
    if (i > 0) {
      int interval = (genome.note[i] + genome.octave[i] * 12) - (genome.note[i-1] + genome.octave[i-1] * 12);
      score -= abs(interval) * 4;
      if (interval == 0 && genome.gate[i] == 1 && genome.gate[i-1] == 1 && genome.tie[i-1] == 0) score -= 15;
      if (genome.tie[i-1] == 1 && (genome.gate[i] == 0 || interval != 0)) score -= 25;
      if (genome.tie[i-1] == 1 && interval == 0 && genome.gate[i] == 1) score += 15;
      if (genome.slide[i-1] == 1 && (genome.gate[i] == 0 || interval == 0)) score -= 20;
    }
  }
  return score;
}

void evaluatePopulation() {
  int best_s = -999999, best_i = 0;
  for (int i = 0; i < POP_SIZE; i++) {
    int s = evaluateGenome(population[i]);
    if (s > best_s) { best_s = s; best_i = i; }
  }
  xSemaphoreTake(genome_mutex, portMAX_DELAY);
  best_genome = population[best_i];
  xSemaphoreGive(genome_mutex);
}

void initPopulation() {
  for (int i = 0; i < POP_SIZE; i++) {
    for (int j = 0; j < NUM_STEPS; j++) {
      population[i].note[j] = random(NUM_NOTES); population[i].octave[j] = random(3);
      population[i].waveform[j] = random(4); population[i].gate[j] = (random(100) < (h_state.groove_density * 100)) ? 1 : 0;
      population[i].tie[j] = (random(100) < 15) ? 1 : 0; population[i].slide[j] = (random(100) < 10) ? 1 : 0;
    }
  }
}

void mutatePopulation(float rate = 0.05) {
  next_gen[0] = best_genome;
  for (int i = 1; i < POP_SIZE; i++) {
    int i1 = random(POP_SIZE), i2 = random(POP_SIZE);
    Genome &p1 = (evaluateGenome(population[i1]) > evaluateGenome(population[i2])) ? population[i1] : population[i2];
    int i3 = random(POP_SIZE), i4 = random(POP_SIZE);
    Genome &p2 = (evaluateGenome(population[i3]) > evaluateGenome(population[i4])) ? population[i3] : population[i4];
    int cp1 = random(NUM_STEPS), cp2 = random(NUM_STEPS);
    if (cp1 > cp2) std::swap(cp1, cp2);
    for(int j=0; j<NUM_STEPS; j++) {
      bool from_p1 = (j < cp1 || j > cp2);
      next_gen[i].note[j] = from_p1 ? p1.note[j] : p2.note[j];
      next_gen[i].octave[j] = from_p1 ? p1.octave[j] : p2.octave[j];
      next_gen[i].waveform[j] = from_p1 ? p1.waveform[j] : p2.waveform[j];
      next_gen[i].gate[j] = from_p1 ? p1.gate[j] : p2.gate[j];
      next_gen[i].tie[j] = from_p1 ? p1.tie[j] : p2.tie[j];
      next_gen[i].slide[j] = from_p1 ? p1.slide[j] : p2.slide[j];
      if ((float)random(100)/100.0 < rate) {
        next_gen[i].note[j] = random(NUM_NOTES); next_gen[i].octave[j] = random(3);
        next_gen[i].waveform[j] = random(4);
        next_gen[i].gate[j] = (random(100) < (h_state.groove_density * 100)) ? 1 : 0;
        next_gen[i].tie[j] = (random(100) < 15) ? 1 : 0; next_gen[i].slide[j] = (random(100) < 10) ? 1 : 0;
      }
    }
  }
  for(int i=0; i<POP_SIZE; i++) population[i] = next_gen[i];
}

// --- Sequencer Task ---
void sequencerTask(void * pvParameters) {
  while(1) {
    int bpm = analogRead(BPM_PIN) / 16 + 60;
    int base_step_time = 60000 / bpm / 4;
    int current_wait = (current_step % 2 == 1) ? (int)(base_step_time * (1.0 + h_state.swing)) : (int)(base_step_time * (1.0 - h_state.swing));

    xSemaphoreTake(genome_mutex, portMAX_DELAY);
    byte note = best_genome.note[current_step], octave = best_genome.octave[current_step];
    byte gate = best_genome.gate[current_step], waveform = best_genome.waveform[current_step];
    byte tie = best_genome.tie[current_step], slide = best_genome.slide[current_step];
    int prev_idx = (current_step == 0) ? NUM_STEPS - 1 : current_step - 1;
    byte prev_note = best_genome.note[prev_idx], prev_octave = best_genome.octave[prev_idx], prev_tie = best_genome.tie[prev_idx], prev_slide = best_genome.slide[prev_idx];
    xSemaphoreGive(genome_mutex);

    int freq = freq_lut[note + octave * 12 + 24];
    uint32_t p_inc = (uint32_t)(((double)freq / SAMPLE_RATE) * 4294967296.0);
    if (gate == 1) {
      bool is_tie = (prev_tie == 1 && note == prev_note && octave == prev_octave), is_slide = (prev_slide == 1);
      current_waveform = waveform; target_phase_inc = p_inc;
      if (!is_slide) phase_inc = p_inc;
      if (!is_tie && !is_slide) amplitude = 0xFFFFFFFF;
      Serial.print(is_tie ? "T" : (is_slide ? "S" : "")); Serial.print(freq);
    } else { Serial.print(0); target_phase_inc = 0; }
    Serial.print(current_step == NUM_STEPS - 1 ? "\n" : " ");

    current_step = (current_step + 1) % NUM_STEPS;
    beat_on = !beat_on; digitalWrite(BEAT_PIN, beat_on ? HIGH : LOW);
    decay_rate = 0xFFFFFFFF / (SAMPLE_RATE * (base_step_time * 1.5) / 1000);
    vTaskDelay(pdMS_TO_TICKS(current_wait));
  }
}

void processSerial() {
  if (Serial.available() > 0) {
    String cmd = Serial.readStringUntil('\n'); cmd.trim();
    if (cmd == "save") saveState();
    else if (cmd == "load") { loadState(); evaluatePopulation(); }
    else if (cmd == "reset") { initPopulation(); for(int i=0; i<NUM_NOTES; i++) note_bias[i]=0; evaluatePopulation(); }
    else if (cmd == "list") { for(int i=0; i<11; i++) { Serial.print(i); Serial.print(": "); Serial.println(SCALES[i].name); } }
    else if (cmd.startsWith("filter ")) lpf_alpha = cmd.substring(7).toInt();
    else if (cmd.startsWith("swing ")) h_state.swing = cmd.substring(6).toFloat();
    else if (cmd == "help") Serial.println("save, load, reset, list, filter [0-256], swing [0-1], seq [len] [root scale]*");
    else Serial.println("Unknown. Try 'help'.");
  }
}

void setup() {
  Serial.begin(115200); LittleFS.begin(true);
  pinMode(BEAT_PIN, OUTPUT); pinMode(LIKE_PIN, INPUT_PULLUP); pinMode(DISLIKE_PIN, INPUT_PULLUP); pinMode(BPM_PIN, INPUT);
  pinMode(LIKE_LED, OUTPUT); pinMode(DISLIKE_LED, OUTPUT);
  genome_mutex = xSemaphoreCreateMutex();
  h_state.sequence[0] = {0, 0}; h_state.sequence[1] = {5, 0}; h_state.sequence[2] = {7, 2}; h_state.sequence[3] = {0, 0};
  initAudio(); initPopulation(); loadState(); evaluatePopulation();
  xTaskCreatePinnedToCore(sequencerTask, "Sequencer", 4096, NULL, 5, &SequencerTask, 1);
}

void loop() {
  processSerial();
  bool like = digitalRead(LIKE_PIN), dislike = digitalRead(DISLIKE_PIN);
  if (like == LOW && last_like == HIGH && (millis() - like_time > DEBOUNCE)) {
    digitalWrite(LIKE_LED, HIGH);
    xSemaphoreTake(genome_mutex, portMAX_DELAY);
    for(int i=0; i<NUM_STEPS; i++) if (best_genome.gate[i]) note_bias[best_genome.note[i]]++;
    xSemaphoreGive(genome_mutex);
    for(int g=0; g<10; g++) mutatePopulation(0.15); evaluatePopulation();
    like_time = millis(); Serial.println("\nLiked");
    digitalWrite(LIKE_LED, LOW);
  } else if (dislike == LOW && last_dislike == HIGH && (millis() - dislike_time > DEBOUNCE)) {
    digitalWrite(DISLIKE_LED, HIGH);
    initPopulation(); for(int i=0; i<NUM_NOTES; i++) note_bias[i] = 0; evaluatePopulation();
    dislike_time = millis(); Serial.println("\nDisliked");
    digitalWrite(DISLIKE_LED, LOW);
  }
  last_like = like; last_dislike = dislike;
  vTaskDelay(10);
}
