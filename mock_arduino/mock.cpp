#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <cstring>
#include <fstream>
#include "bass_logic.h"

HarmonicState h_state;
Genome population[POP_SIZE], next_gen[POP_SIZE], best_genome;
int note_bias[NUM_NOTES] = {0}, freq_lut[128];

void initFreqLUT() { for (int i = 0; i < 128; i++) freq_lut[i] = (int)(440.0 * pow(2.0, (i - 69) / 12.0)); }

void saveState(const std::string& filename) {
    std::ofstream f(filename, std::ios::binary);
    if (!f) return;
    f.write((char*)&best_genome, sizeof(best_genome));
    f.write((char*)note_bias, sizeof(note_bias));
    f.write((char*)&h_state, sizeof(h_state));
    f.close();
}

void loadState(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f) return;
    f.read((char*)&best_genome, sizeof(best_genome));
    f.read((char*)note_bias, sizeof(note_bias));
    f.read((char*)&h_state, sizeof(h_state));
    f.close();
    population[0] = best_genome;
}

int main(int argc, char** argv) {
    srand(time(NULL)); initFreqLUT();
    h_state.sequence[0] = {0, 0}; h_state.sequence[1] = {5, 0}; h_state.sequence[2] = {7, 2}; h_state.sequence[3] = {0, 0};
    initPopulation(population, h_state);

    int bs = -999999, bi = 0;
    for(int i=0; i<POP_SIZE; i++) { int s = evaluateGenome(population[i], h_state, note_bias); if(s>bs){bs=s; bi=i;}}
    best_genome = population[bi];

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "iters") { if (i+1 < argc) { int ic = atoi(argv[++i]); for(int it=0; it<ic; it++) { mutatePopulation(population, next_gen, best_genome, h_state, note_bias, 0.05); for(int j=0; j<POP_SIZE; j++) population[j] = next_gen[j]; int bbest=-999999, bbi=0; for(int j=0; j<POP_SIZE; j++){int s=evaluateGenome(population[j], h_state, note_bias); if(s>bbest){bbest=s; bbi=j;}} best_genome=population[bbi]; } } }
    }

    for (int i = 0; i < NUM_STEPS; i++) {
        if (best_genome.gate[i]) {
            int midi = best_genome.note[i] + best_genome.octave[i] * 12 + 24;
            int p = (i == 0) ? NUM_STEPS - 1 : i - 1;
            bool is_tie = (best_genome.tie[p] == 1 && best_genome.note[i] == best_genome.note[p] && best_genome.octave[i] == best_genome.octave[p]);
            bool is_slide = (best_genome.slide[p] == 1);
            if (is_tie) std::cout << "T"; else if (is_slide) std::cout << "S";
            std::cout << freq_lut[midi] << " ";
        } else std::cout << 0 << " ";
    }
    std::cout << std::endl; return 0;
}
