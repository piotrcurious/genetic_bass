#ifndef BASS_LOGIC_H
#define BASS_LOGIC_H

#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <cstdint>

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

inline int evaluateGenome(const Genome& g, const HarmonicState& h, const int* bias) {
  int score = 0;
  for (int i = 0; i < NUM_STEPS; i++) {
    int ci = (i * h.sequence_len) / NUM_STEPS;
    Chord c = h.sequence[ci];
    uint16_t mask = SCALES[c.scale_idx].mask;
    byte rel = (g.note[i] - c.root + 12) % 12;
    score += ((mask >> rel) & 1) ? 20 : -100;
    if (rel == 0) score += 25; else if (rel == 7) score += 10;
    score += bias[g.note[i]] * 5;
    if (i % 4 == 0) { if (g.gate[i] == 1) score += 10; else score -= 15; }
    if (i > 0) {
      int interval = (g.note[i] + g.octave[i] * 12) - (g.note[i-1] + g.octave[i-1] * 12);
      score -= abs(interval) * 4;
      if (interval == 0 && g.gate[i] == 1 && g.gate[i-1] == 1 && g.tie[i-1] == 0) score -= 15;
      if (g.tie[i-1] == 1 && (g.gate[i] == 0 || interval != 0)) score -= 25;
      if (g.tie[i-1] == 1 && interval == 0 && g.gate[i] == 1) score += 15;
      if (g.slide[i-1] == 1 && (g.gate[i] == 0 || interval == 0)) score -= 20;
    }
  }
  return score;
}

inline void initPopulation(Genome* pop, const HarmonicState& h) {
  for (int i = 0; i < POP_SIZE; i++) {
    for (int j = 0; j < NUM_STEPS; j++) {
      pop[i].note[j] = rand() % NUM_NOTES; pop[i].octave[j] = rand() % 3;
      pop[i].waveform[j] = rand() % 4; pop[i].gate[j] = (rand() % 100 < (h.groove_density * 100)) ? 1 : 0;
      pop[i].tie[j] = (rand() % 100 < 15) ? 1 : 0; pop[i].slide[j] = (rand() % 100 < 10) ? 1 : 0;
    }
  }
}

inline void mutatePopulation(Genome* pop, Genome* next, const Genome& best, const HarmonicState& h, const int* bias, float rate) {
  next[0] = best;
  for (int i = 1; i < POP_SIZE; i++) {
    int i1 = rand() % POP_SIZE, i2 = rand() % POP_SIZE;
    const Genome &p1 = (evaluateGenome(pop[i1], h, bias) > evaluateGenome(pop[i2], h, bias)) ? pop[i1] : pop[i2];
    int i3 = rand() % POP_SIZE, i4 = rand() % POP_SIZE;
    const Genome &p2 = (evaluateGenome(pop[i3], h, bias) > evaluateGenome(pop[i4], h, bias)) ? pop[i3] : pop[i4];
    int cp1 = rand() % NUM_STEPS, cp2 = rand() % NUM_STEPS;
    if (cp1 > cp2) std::swap(cp1, cp2);
    for(int j=0; j<NUM_STEPS; j++) {
      bool from_p1 = (j < cp1 || j > cp2);
      next[i].note[j] = from_p1 ? p1.note[j] : p2.note[j];
      next[i].octave[j] = from_p1 ? p1.octave[j] : p2.octave[j];
      next[i].waveform[j] = from_p1 ? p1.waveform[j] : p2.waveform[j];
      next[i].gate[j] = from_p1 ? p1.gate[j] : p2.gate[j];
      next[i].tie[j] = from_p1 ? p1.tie[j] : p2.tie[j];
      next[i].slide[j] = from_p1 ? p1.slide[j] : p2.slide[j];
      if ((float)rand()/RAND_MAX < rate) {
        next[i].note[j] = rand() % NUM_NOTES; next[i].octave[j] = rand() % 3;
        next[i].waveform[j] = rand() % 4; next[i].gate[j] = (rand() % 100 < (h.groove_density * 100)) ? 1 : 0;
        next[i].tie[j] = (rand() % 100 < 15) ? 1 : 0; next[i].slide[j] = (rand() % 100 < 10) ? 1 : 0;
      }
    }
  }
}

#endif
