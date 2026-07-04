#ifndef GENETIC_BASS_CORE_H
#define GENETIC_BASS_CORE_H

#ifdef ARDUINO
#include <Arduino.h>
#define RANDOM_INT(n) random(n)
#define MUTEX_TAKE() xSemaphoreTake(genome_mutex, portMAX_DELAY)
#define MUTEX_GIVE() xSemaphoreGive(genome_mutex)
#else
#include <cstdlib>
#include <algorithm>
#define RANDOM_INT(n) (rand() % (n))
#define MUTEX_TAKE()
#define MUTEX_GIVE()
typedef unsigned char byte;
#endif

// --- Constants ---
const int NUM_STEPS = 64;
const int NUM_NOTES = 12;
const int POP_SIZE = 100;

// --- Structs ---
struct Genome {
  byte note[NUM_STEPS];
  byte octave[NUM_STEPS];
  byte waveform[NUM_STEPS];
  byte gate[NUM_STEPS];
  byte tie[NUM_STEPS];
  byte slide[NUM_STEPS];
};

struct Scale {
  const char* name;
  uint16_t mask;
};

const Scale SCALES[] = {
  {"Major", 0b101010110101}, {"Minor", 0b010110101101}, {"Dorian", 0b011010101101},
  {"Phrygian", 0b010110101011}, {"Lydian", 0b101010100101}, {"Mixolydian", 0b011010110101},
  {"Locrian", 0b010101101011}, {"Pentatonic Maj", 0b101000110101},
  {"Pentatonic Min", 0b010100101001}, {"Blues", 0b010101101001}, {"Chromatic", 0b111111111111}
};

struct Chord {
  byte root = 0;
  byte scale_idx = 0;
};

struct HarmonicState {
  Chord sequence[8];
  byte sequence_len = 4;
  float groove_density = 0.6;
  float syncopation_bias = 0.5;
  float swing = 0.0;
};

// --- Globals (extern) ---
extern HarmonicState h_state;
extern Genome population[POP_SIZE], next_gen[POP_SIZE], best_genome;
extern int pop_fitness[POP_SIZE];
extern int note_bias[NUM_NOTES];

#ifdef ARDUINO
extern SemaphoreHandle_t genome_mutex;
#endif

// --- Core Functions ---
inline int evaluateGenome(const Genome& genome) {
  int score = 0;
  for (int i = 0; i < NUM_STEPS; i++) {
    int ci = (i * h_state.sequence_len) / NUM_STEPS;
    Chord c = h_state.sequence[ci];
    uint16_t mask = SCALES[c.scale_idx].mask;
    byte rel = (genome.note[i] - c.root + 12) % 12;

    // Scale matching
    score += ((mask >> rel) & 1) ? 20 : -100;
    if (rel == 0) score += 25;
    else if (rel == 7) score += 10;

    // Bias and rhythm
    score += note_bias[genome.note[i]] * 5;
    if (i % 4 == 0) {
      score += (genome.gate[i] == 1) ? 10 : -15;
    }

    // Melodic contour and articulation rules
    if (i > 0) {
      int interval = (genome.note[i] + genome.octave[i] * 12) - (genome.note[i-1] + genome.octave[i-1] * 12);
      score -= abs(interval) * 4; // Penalize massive leaps

      if (interval == 0 && genome.gate[i] == 1 && genome.gate[i-1] == 1 && genome.tie[i-1] == 0) score -= 15; // Machine-gun penalty
      if (genome.tie[i-1] == 1 && (genome.gate[i] == 0 || interval != 0)) score -= 25; // Broken tie penalty
      if (genome.tie[i-1] == 1 && interval == 0 && genome.gate[i] == 1) score += 15; // Good tie reward
      if (genome.slide[i-1] == 1 && (genome.gate[i] == 0 || interval == 0)) score -= 20; // Bad slide penalty
    }
  }
  return score;
}

inline void evaluatePopulation() {
  int best_s = -999999;
  int best_idx = 0;

  for (int i = 0; i < POP_SIZE; i++) {
    pop_fitness[i] = evaluateGenome(population[i]); // Cache fitness
    if (pop_fitness[i] > best_s) {
      best_s = pop_fitness[i];
      best_idx = i;
    }
  }

  MUTEX_TAKE();
  best_genome = population[best_idx];
  MUTEX_GIVE();
}

inline void initPopulation() {
  for (int i = 0; i < POP_SIZE; i++) {
    for (int j = 0; j < NUM_STEPS; j++) {
      population[i].note[j] = RANDOM_INT(NUM_NOTES);
      population[i].octave[j] = RANDOM_INT(3);
      population[i].waveform[j] = RANDOM_INT(4);
      population[i].gate[j] = (RANDOM_INT(100) < (h_state.groove_density * 100)) ? 1 : 0;
      population[i].tie[j] = (RANDOM_INT(100) < 15) ? 1 : 0;
      population[i].slide[j] = (RANDOM_INT(100) < 10) ? 1 : 0;
    }
  }
}

inline void mutatePopulation(float rate = 0.05) {
  next_gen[0] = best_genome; // Elitism: carry over the best

  for (int i = 1; i < POP_SIZE; i++) {
    // Tournament Selection using CACHED fitness
    int i1 = RANDOM_INT(POP_SIZE), i2 = RANDOM_INT(POP_SIZE);
    Genome &p1 = (pop_fitness[i1] > pop_fitness[i2]) ? population[i1] : population[i2];

    int i3 = RANDOM_INT(POP_SIZE), i4 = RANDOM_INT(POP_SIZE);
    Genome &p2 = (pop_fitness[i3] > pop_fitness[i4]) ? population[i3] : population[i4];

    int cp1 = RANDOM_INT(NUM_STEPS), cp2 = RANDOM_INT(NUM_STEPS);
    if (cp1 > cp2) std::swap(cp1, cp2);

    // Crossover & Mutation
    for(int j = 0; j < NUM_STEPS; j++) {
      bool from_p1 = (j < cp1 || j > cp2);

      next_gen[i].note[j] = from_p1 ? p1.note[j] : p2.note[j];
      next_gen[i].octave[j] = from_p1 ? p1.octave[j] : p2.octave[j];
      next_gen[i].waveform[j] = from_p1 ? p1.waveform[j] : p2.waveform[j];
      next_gen[i].gate[j] = from_p1 ? p1.gate[j] : p2.gate[j];
      next_gen[i].tie[j] = from_p1 ? p1.tie[j] : p2.tie[j];
      next_gen[i].slide[j] = from_p1 ? p1.slide[j] : p2.slide[j];

      if ((float)RANDOM_INT(1000)/1000.0 < rate) {
        next_gen[i].note[j] = RANDOM_INT(NUM_NOTES);
        next_gen[i].octave[j] = RANDOM_INT(3);
        next_gen[i].waveform[j] = RANDOM_INT(4);
        next_gen[i].gate[j] = (RANDOM_INT(100) < (h_state.groove_density * 100)) ? 1 : 0;
        next_gen[i].tie[j] = (RANDOM_INT(100) < 15) ? 1 : 0;
        next_gen[i].slide[j] = (RANDOM_INT(100) < 10) ? 1 : 0;
      }
    }
  }

  for(int i = 0; i < POP_SIZE; i++) {
    population[i] = next_gen[i];
  }
}

#endif
