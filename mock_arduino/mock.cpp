#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <cstdint>
#include <string>

typedef unsigned char byte;

const int NUM_STEPS = 64;
const int NUM_NOTES = 12;
const int POP_SIZE = 100;

struct Genome {
    byte note[NUM_STEPS];
    byte octave[NUM_STEPS];
    byte waveform[NUM_STEPS];
    byte gate[NUM_STEPS];
    byte tie[NUM_STEPS];
};

struct Scale {
  const char* name;
  uint16_t mask;
};

const Scale SCALES[] = {
  {"Major", 0b101010110101}, {"Minor", 0b010110101101}, {"Dorian", 0b011010101101}, {"Phrygian", 0b010110101011},
  {"Lydian", 0b101010100101}, {"Mixolydian", 0b011010110101}, {"Locrian", 0b010101101011}, {"Pentatonic Maj", 0b101000110101},
  {"Pentatonic Min", 0b010100101001}, {"Blues", 0b010101101001}, {"Chromatic", 0b111111111111}
};

struct HarmonicState {
  byte root = 0;
  byte scale_idx = 0;
  float groove_density = 0.6;
  float syncopation_bias = 0.5;
};

HarmonicState h_state;
Genome population[POP_SIZE];
Genome next_gen[POP_SIZE];
Genome best_genome;
int note_bias[NUM_NOTES] = {0};
int freq_lut[128];

void initFreqLUT() { for (int i = 0; i < 128; i++) freq_lut[i] = (int)(440.0 * pow(2.0, (i - 69) / 12.0)); }

int evaluateGenome(const Genome& genome) {
  int score = 0;
  uint16_t mask = SCALES[h_state.scale_idx].mask;
  for (int i = 0; i < NUM_STEPS; i++) {
    byte rel = (genome.note[i] - h_state.root + 12) % 12;
    bool in_scale = (mask >> rel) & 1;
    score += in_scale ? 15 : -30;
    if (rel == 0) score += 20; else if (rel == 7) score += 10;
    score += note_bias[genome.note[i]] * 5;
    if (i % 4 == 0) {
        if (genome.gate[i] == 1) score += 10; else score -= 15;
    } else { if (genome.gate[i] == 1) score += (int)(h_state.syncopation_bias * 10); }
    if (genome.gate[i] == 1) score -= (int)(abs(h_state.groove_density - 0.5) * 5);
    if (i > 0) {
      int interval = (genome.note[i] + genome.octave[i] * 12) - (genome.note[i-1] + genome.octave[i-1] * 12);
      score -= abs(interval) * 4;
      if (interval == 0 && genome.gate[i] == 1 && genome.gate[i-1] == 1 && genome.tie[i-1] == 0) score -= 15;
      if (genome.tie[i-1] == 1 && (genome.gate[i] == 0 || interval != 0)) score -= 20;
      if (genome.tie[i-1] == 1 && interval == 0 && genome.gate[i] == 1) score += 15;
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
  best_genome = population[best_i];
}

void initPopulation() {
  for (int i = 0; i < POP_SIZE; i++) {
    for (int j = 0; j < NUM_STEPS; j++) {
      population[i].note[j] = rand() % NUM_NOTES;
      population[i].octave[j] = rand() % 3;
      population[i].waveform[j] = rand() % 4;
      population[i].gate[j] = (rand() % 100 < (h_state.groove_density * 100)) ? 1 : 0;
      population[i].tie[j] = (rand() % 100 < 15) ? 1 : 0;
    }
  }
}

void mutatePopulation(float rate = 0.05) {
  next_gen[0] = best_genome;
  for (int i = 1; i < POP_SIZE; i++) {
    Genome p1 = best_genome, p2 = population[rand() % POP_SIZE];
    int cp1 = rand() % NUM_STEPS, cp2 = rand() % NUM_STEPS;
    if (cp1 > cp2) std::swap(cp1, cp2);
    for(int j=0; j<NUM_STEPS; j++) {
      bool from_p1 = (j < cp1 || j > cp2);
      next_gen[i].note[j] = from_p1 ? p1.note[j] : p2.note[j];
      next_gen[i].octave[j] = from_p1 ? p1.octave[j] : p2.octave[j];
      next_gen[i].gate[j] = from_p1 ? p1.gate[j] : p2.gate[j];
      next_gen[i].tie[j] = from_p1 ? p1.tie[j] : p2.tie[j];
      next_gen[i].waveform[j] = p1.waveform[j];
      if ((float)rand()/RAND_MAX < rate) {
        next_gen[i].note[j] = rand() % NUM_NOTES;
        next_gen[i].octave[j] = rand() % 3;
        next_gen[i].gate[j] = (rand() % 100 < (h_state.groove_density * 100)) ? 1 : 0;
        next_gen[i].tie[j] = (rand() % 100 < 15) ? 1 : 0;
      }
    }
  }
  for(int i=0; i<POP_SIZE; i++) population[i] = next_gen[i];
}

int main(int argc, char** argv) {
    srand(time(NULL)); initFreqLUT();
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "scale") { if (i+1 < argc) h_state.scale_idx = atoi(argv[++i]) % 11; }
        else if (arg == "root") { if (i+1 < argc) h_state.root = atoi(argv[++i]) % 12; }
        else if (arg == "groove") { if (i+1 < argc) h_state.groove_density = atof(argv[++i]); }
    }
    initPopulation(); evaluatePopulation();
    for (int it = 0; it < 300; it++) { mutatePopulation(); evaluatePopulation(); }
    for (int i = 0; i < NUM_STEPS; i++) {
        if (best_genome.gate[i]) {
            int midi = best_genome.note[i] + best_genome.octave[i] * 12 + 24;
            int p = (i == 0) ? NUM_STEPS - 1 : i - 1;
            if (best_genome.tie[p] == 1 && best_genome.note[i] == best_genome.note[p] && best_genome.octave[i] == best_genome.octave[p]) std::cout << "T" << freq_lut[midi] << " ";
            else std::cout << freq_lut[midi] << " ";
        } else std::cout << 0 << " ";
    }
    std::cout << std::endl; return 0;
}
