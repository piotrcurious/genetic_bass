#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <ctime>
#include <cstdlib>

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

const int JAZZ_TABLE[7][12] = {
    {10, -5, 2, -5, 8, 5, -5, 10, -5, 5, -5, 8},
    {10, -5, 5, 10, -5, 5, -5, 10, -5, 2, 8, -5},
    {10, -5, 2, -5, 8, 5, -5, 10, -5, 5, 8, -5},
    {10, -5, -5, 10, -5, -5, 10, -5, -5, 10, -5, -5},
    {10, -5, -5, 10, -5, -5, 8, -5, -5, 2, 8, -5},
    {10, -5, -5, -5, 10, -5, -5, 8, -5, -5, -5, -5},
    {10, -5, -5, -5, -5, 10, 8, 10, -5, -5, -5, 8}
};

enum Progression { HOUSE, BLUES, JAZZ_II_V_I, FUNK, POP, MINOR_BLUES, ROCK };
Progression current_prog = HOUSE;

const byte PROGRESSIONS[7][4][2] = {
  {{0,0}, {1,9}, {2,7}, {1,2}},
  {{2,0}, {2,5}, {2,0}, {2,7}},
  {{1,2}, {2,7}, {0,0}, {0,0}},
  {{1,4}, {1,4}, {2,9}, {2,9}},
  {{0,0}, {0,7}, {1,9}, {0,5}},
  {{1,0}, {1,5}, {1,0}, {1,7}},
  {{0,2}, {0,0}, {0,7}, {0,5}}
};

int freq_lut[128];
void initFreqLUT() {
    for (int i = 0; i < 128; i++) freq_lut[i] = (int)(440.0 * pow(2.0, (i - 69) / 12.0));
}

Genome population[POP_SIZE];
Genome next_gen[POP_SIZE];
Genome best_genome;
int best_score;
int note_bias[NUM_NOTES] = {0};
int octave_bias[3] = {0};

int mtof(byte note, byte octave) {
    int midi = note + octave * 12 + 24;
    return (midi >= 0 && midi < 128) ? freq_lut[midi] : 0;
}

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
        if (i % 4 == 0) {
            if (genome.note[i] == chord_root) score += 20;
            if (genome.gate[i] == 0) score -= 10;
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
    best_score = -999999;
    for (int i = 0; i < POP_SIZE; i++) {
        int score = evaluateGenome(population[i]);
        if (score > best_score) { best_score = score; best_genome = population[i]; }
    }
}

void initPopulation() {
    for (int i = 0; i < POP_SIZE; i++) {
        for (int j = 0; j < NUM_STEPS; j++) {
            population[i].note[j] = rand() % NUM_NOTES;
            population[i].octave[j] = rand() % 3;
            population[i].waveform[j] = rand() % 4;
            population[i].gate[j] = (rand() % 10 < 7) ? 1 : 0;
            population[i].tie[j] = (rand() % 10 < 2) ? 1 : 0;
        }
    }
}

void mutatePopulation(float rate = 0.05) {
    next_gen[0] = best_genome;
    for (int i = 1; i < POP_SIZE; i++) {
        Genome parent1 = best_genome;
        Genome parent2 = population[rand() % POP_SIZE];
        int p1 = rand() % NUM_STEPS, p2 = rand() % NUM_STEPS;
        if (p1 > p2) std::swap(p1, p2);
        for(int j=0; j<NUM_STEPS; j++) {
            bool from_p1 = (j < p1 || j > p2);
            next_gen[i].note[j] = from_p1 ? parent1.note[j] : parent2.note[j];
            next_gen[i].octave[j] = from_p1 ? parent1.octave[j] : parent2.octave[j];
            next_gen[i].gate[j] = from_p1 ? parent1.gate[j] : parent2.gate[j];
            next_gen[i].tie[j] = from_p1 ? parent1.tie[j] : parent2.tie[j];
            next_gen[i].waveform[j] = parent1.waveform[j];
            if ((float)rand()/RAND_MAX < rate) {
                next_gen[i].note[j] = rand() % NUM_NOTES;
                next_gen[i].octave[j] = rand() % 3;
                next_gen[i].gate[j] = (rand() % 10 < 7) ? 1 : 0;
                next_gen[i].tie[j] = (rand() % 10 < 2) ? 1 : 0;
            }
        }
    }
    for(int i=0; i<POP_SIZE; i++) population[i] = next_gen[i];
}

void updateBias() {
    for(int i=0; i<NUM_STEPS; i++) if (best_genome.gate[i]) { note_bias[best_genome.note[i]]++; octave_bias[best_genome.octave[i]]++; }
}

int main(int argc, char** argv) {
    srand(time(NULL));
    initFreqLUT();
    int iters = 100, prog_idx = 0, likes = 0;
    if (argc > 1) iters = atoi(argv[1]);
    if (argc > 2) prog_idx = atoi(argv[2]);
    if (argc > 3) likes = atoi(argv[3]);
    current_prog = (Progression)(prog_idx % 7);
    initPopulation();
    evaluatePopulation();
    for (int l = 0; l <= likes; l++) {
        for (int it = 0; it < iters; it++) { mutatePopulation(); evaluatePopulation(); }
        if (l < likes) updateBias();
    }
    for (int i = 0; i < NUM_STEPS; i++) {
        if (best_genome.gate[i]) {
            int f = mtof(best_genome.note[i], best_genome.octave[i]);
            if (i > 0 && best_genome.tie[i-1] == 1 && best_genome.note[i] == best_genome.note[i-1] && best_genome.octave[i] == best_genome.octave[i-1]) std::cout << "T" << f << " ";
            else std::cout << f << " ";
        } else std::cout << 0 << " ";
    }
    std::cout << std::endl;
    return 0;
}
