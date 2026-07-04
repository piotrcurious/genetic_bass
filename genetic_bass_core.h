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
  int consecutive_notes = 0;
  int notes_played = 0;

  // Step-wise evaluation
  for (int i = 0; i < NUM_STEPS; i++) {
    int ci = (i * h_state.sequence_len) / NUM_STEPS;
    Chord c = h_state.sequence[ci];
    uint16_t mask = SCALES[c.scale_idx].mask;
    byte rel = (genome.note[i] - c.root + 12) % 12;

    // Scale matching
    bool in_scale = (mask >> rel) & 1;
    score += in_scale ? 25 : -150;
    if (rel == 0) score += 40; // Strong root reward
    else if (rel == 7) score += 20; // Fifth
    else if (rel == 4 || rel == 3) score += 15; // Third

    // Bias
    score += note_bias[genome.note[i]] * 5;

    // Rhythm and Syncopation
    if (genome.gate[i] == 1) {
      notes_played++;
      consecutive_notes++;
      if (consecutive_notes > 3) score -= 30; // Penalty for too many consecutive notes

      bool on_beat = (i % 4 == 0);
      bool off_beat = (i % 2 == 1);

      if (on_beat) {
        score += 20;
      } else if (off_beat) {
        score += (int)(h_state.syncopation_bias * 50);
      }

      // Slap-style octave jumps on off-beats
      if (off_beat && i > 0 && genome.gate[i-1] == 1) {
          int prev_p = genome.note[i-1] + genome.octave[i-1] * 12;
          int curr_p = genome.note[i] + genome.octave[i] * 12;
          if (curr_p - prev_p == 12) score += 40;
      }
    } else {
      consecutive_notes = 0;
      if (i % 4 == 0) score -= 40; // Stronger beat emphasis
    }

    // Melodic contour
    if (i > 0) {
      int prev_pitch = genome.note[i-1] + genome.octave[i-1] * 12;
      int curr_pitch = genome.note[i] + genome.octave[i] * 12;
      int interval = curr_pitch - prev_pitch;
      int abs_interval = abs(interval);

      score -= abs_interval * 2;

      if (abs_interval == 1 || abs_interval == 2) score += 15; // Step-wise motion reward
      if (abs_interval == 12) score += 30; // Octave jumps
      if (abs_interval == 7) score += 20; // Fifths

      // Leap and return
      if (i > 1) {
          int prev_prev_pitch = genome.note[i-2] + genome.octave[i-2] * 12;
          int prev_interval = prev_pitch - prev_prev_pitch;
          if (abs(prev_interval) > 4) {
              if ((prev_interval > 0 && interval < 0) || (prev_interval < 0 && interval > 0)) {
                  score += 25;
              }
          }
      }

      // Approach notes to next chord
      if (i < NUM_STEPS - 1) {
          int next_ci = ((i + 1) * h_state.sequence_len) / NUM_STEPS;
          if (next_ci != ci) {
              int next_root = h_state.sequence[next_ci].root;
              int dist_to_next_root = abs((curr_pitch % 12) - next_root);
              if (dist_to_next_root == 1 || dist_to_next_root == 11) score += 40; // Chromatic approach
          }
      }

      // Articulation
      if (interval == 0 && genome.gate[i] == 1 && genome.gate[i-1] == 1 && genome.tie[i-1] == 0) score -= 50; // No machine guns
      if (genome.tie[i-1] == 1 && (genome.gate[i] == 0 || interval != 0)) score -= 40;
      if (genome.tie[i-1] == 1 && interval == 0 && genome.gate[i] == 1) score += 25;
      if (genome.slide[i-1] == 1 && (genome.gate[i] == 0 || interval == 0)) score -= 30;
    }
  }

  // Density reward (aim for ~ideal density)
  int ideal_notes = (int)(NUM_STEPS * h_state.groove_density);
  score -= abs(notes_played - ideal_notes) * 30;

  // Motif Repetition Detection (Functional/Relative)
  for (int b = 0; b < NUM_STEPS / 16 - 1; b++) {
      int ci_b = (b * 16 * h_state.sequence_len) / NUM_STEPS;
      int root_b = h_state.sequence[ci_b].root;

      for (int b2 = b + 1; b2 < NUM_STEPS / 16; b2++) {
          int ci_b2 = (b2 * 16 * h_state.sequence_len) / NUM_STEPS;
          int root_b2 = h_state.sequence[ci_b2].root;

          int functional_matches = 0;
          for (int s = 0; s < 16; s++) {
              int rel_b = (genome.note[b*16+s] - root_b + 12) % 12;
              int rel_b2 = (genome.note[b2*16+s] - root_b2 + 12) % 12;
              if (rel_b == rel_b2 &&
                  genome.gate[b*16+s] == genome.gate[b2*16+s]) {
                  functional_matches++;
              }
          }
          if (functional_matches >= 10) score += 2000;
          if (functional_matches >= 14) score += 3000;
          if (functional_matches == 16) score += 6000;
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
  next_gen[0] = best_genome; // Elitism

  for (int i = 1; i < POP_SIZE; i++) {
    int i1 = RANDOM_INT(POP_SIZE), i2 = RANDOM_INT(POP_SIZE);
    Genome &p1 = (pop_fitness[i1] > pop_fitness[i2]) ? population[i1] : population[i2];

    int i3 = RANDOM_INT(POP_SIZE), i4 = RANDOM_INT(POP_SIZE);
    Genome &p2 = (pop_fitness[i3] > pop_fitness[i4]) ? population[i3] : population[i4];

    int cp1 = RANDOM_INT(NUM_STEPS), cp2 = RANDOM_INT(NUM_STEPS);
    if (cp1 > cp2) std::swap(cp1, cp2);

    for(int j = 0; j < NUM_STEPS; j++) {
      // Scale-aware functional duplication from parent
      if (j % 16 == 0 && RANDOM_INT(100) < 40) {
          int source_block = RANDOM_INT(NUM_STEPS / 16) * 16;
          int root_s = h_state.sequence[(source_block * h_state.sequence_len) / NUM_STEPS].root;
          int root_t = h_state.sequence[(j * h_state.sequence_len) / NUM_STEPS].root;

          for (int s = 0; s < 16 && j + s < NUM_STEPS; s++) {
              int rel = (p1.note[source_block+s] - root_s + 12) % 12;
              next_gen[i].note[j+s] = (rel + root_t) % 12;
              next_gen[i].gate[j+s] = p1.gate[source_block+s];
              next_gen[i].octave[j+s] = p1.octave[source_block+s];
              next_gen[i].waveform[j+s] = p1.waveform[source_block+s];
              next_gen[i].tie[j+s] = p1.tie[source_block+s];
              next_gen[i].slide[j+s] = p1.slide[source_block+s];
          }
          j += 15; continue;
      }

      bool from_p1 = (j < cp1 || j > cp2);
      next_gen[i].note[j] = from_p1 ? p1.note[j] : p2.note[j];
      next_gen[i].octave[j] = from_p1 ? p1.octave[j] : p2.octave[j];
      next_gen[i].waveform[j] = from_p1 ? p1.waveform[j] : p2.waveform[j];
      next_gen[i].gate[j] = from_p1 ? p1.gate[j] : p2.gate[j];
      next_gen[i].tie[j] = from_p1 ? p1.tie[j] : p2.tie[j];
      next_gen[i].slide[j] = from_p1 ? p1.slide[j] : p2.slide[j];

      if ((float)RANDOM_INT(1000)/1000.0 < rate) {
        int r = RANDOM_INT(100);
        if (r < 40) next_gen[i].note[j] = RANDOM_INT(NUM_NOTES);
        else if (r < 60) next_gen[i].octave[j] = RANDOM_INT(3);
        else if (r < 80) next_gen[i].gate[j] = !next_gen[i].gate[j];
        else if (r < 90) next_gen[i].tie[j] = !next_gen[i].tie[j];
        else next_gen[i].slide[j] = !next_gen[i].slide[j];
      }
    }
  }
  for(int i = 0; i < POP_SIZE; i++) population[i] = next_gen[i];
}

#endif
