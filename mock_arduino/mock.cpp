#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <ctime>
#include <cstdlib>

// Mock Arduino constants and types
#define HIGH 1
#define LOW 0
#define INPUT_PULLUP 2
#define OUTPUT 1

typedef unsigned char byte;

// Simulation parameters
const int NUM_STEPS = 64;
const int NUM_NOTES = 12;
const int POP_SIZE = 100;
float MUT_RATE = 0.05;

struct Genome {
    byte note[NUM_STEPS];
    byte octave[NUM_STEPS];
    byte waveform[NUM_STEPS];
    byte gate[NUM_STEPS];
};

// Improved JAZZ_TABLE (12 notes scale)
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
    {0, 0}, // C major
    {1, 9}, // A minor
    {2, 7}, // G dominant
    {1, 2}  // D minor
};

// Pre-computed MIDI to Frequency LUT (Notes 0-127)
int freq_lut[128];
void initFreqLUT() {
    for (int i = 0; i < 128; i++) {
        freq_lut[i] = (int)(440.0 * pow(2.0, (i - 69) / 12.0));
    }
}

Genome population[POP_SIZE];
Genome best_genome;
int best_score;

// User preferences (biased by Likes)
int note_bias[NUM_NOTES] = {0};
int octave_bias[3] = {0};

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

        // Bias from user feedback
        score += note_bias[note] * 2;
        score += octave_bias[octave] * 2;

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
    best_score = -999999;
    for (int i = 0; i < POP_SIZE; i++) {
        int score = evaluateGenome(population[i]);
        if (score > best_score) {
            best_score = score;
            best_genome = population[i];
        }
    }
}

void initPopulation() {
    for (int i = 0; i < POP_SIZE; i++) {
        for (int j = 0; j < NUM_STEPS; j++) {
            population[i].note[j] = rand() % NUM_NOTES;
            population[i].octave[j] = rand() % 3;
            population[i].waveform[j] = rand() % 4;
            population[i].gate[j] = (rand() % 10 < 7) ? 1 : 0;
        }
    }
}

Genome next_gen[POP_SIZE];

void mutatePopulation() {
    next_gen[0] = best_genome;
    for (int i = 1; i < POP_SIZE; i++) {
        Genome parent1 = best_genome;
        Genome parent2 = population[rand() % POP_SIZE];
        Genome child;
        for(int j=0; j<NUM_STEPS; j++) {
            if (rand() % 100 < 50) child.note[j] = parent1.note[j];
            else child.note[j] = parent2.note[j];
            if (rand() % 100 < 50) child.octave[j] = parent1.octave[j];
            else child.octave[j] = parent2.octave[j];
            if (rand() % 100 < 50) child.gate[j] = parent1.gate[j];
            else child.gate[j] = parent2.gate[j];
            child.waveform[j] = parent1.waveform[j];
            if ((float)rand()/RAND_MAX < MUT_RATE) child.note[j] = rand() % NUM_NOTES;
            if ((float)rand()/RAND_MAX < MUT_RATE) child.octave[j] = rand() % 3;
            if ((float)rand()/RAND_MAX < MUT_RATE) child.gate[j] = (rand() % 10 < 7) ? 1 : 0;
        }
        next_gen[i] = child;
    }
    for(int i=0; i<POP_SIZE; i++) population[i] = next_gen[i];
}

int main(int argc, char** argv) {
    srand(time(NULL));
    initFreqLUT();
    int iterations = 100;
    if (argc > 1) iterations = atoi(argv[1]);

    initPopulation();
    evaluatePopulation();

    // Simulate some user likes
    note_bias[0] = 5; // User likes C
    note_bias[7] = 3; // User likes G

    for (int it = 0; it < iterations; it++) {
        mutatePopulation();
        evaluatePopulation();
    }

    for (int i = 0; i < NUM_STEPS; i++) {
        if (best_genome.gate[i]) std::cout << mtof(best_genome.note[i], best_genome.octave[i]) << " ";
        else std::cout << 0 << " ";
    }
    std::cout << std::endl;
    return 0;
}
