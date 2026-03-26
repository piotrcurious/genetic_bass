#include <iostream>
#include <vector>
#include <cmath>
#include <mutex>
#include <portaudio.h>
#include <FL/Fl.H>
#include <FL/Fl_Window.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Value_Slider.H>
#include <FL/Fl_Output.H>
#include <FL/Fl_Choice.H>
#include "bass_logic.h"

// --- Global App State ---
HarmonicState h_state;
Genome population[POP_SIZE], next_gen[POP_SIZE], best_genome;
int note_bias[NUM_NOTES] = {0};
int current_step = 0;
float bpm = 120.0;
int lpf_alpha = 256;
std::mutex state_mutex;

// Audio variables
uint32_t phase = 0, phase_inc = 0, target_p_inc = 0;
uint32_t amplitude = 0, decay_rate = 100000;
int lpf_prev = 128;
byte sine_lut[256];
int freq_lut[128];
int current_waveform = 1;

// --- Audio Callback ---
static int audioCallback(const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void *userData) {
    float *out = (float*)outputBuffer;
    for (unsigned int i = 0; i < framesPerBuffer; i++) {
        if (amplitude > 0) {
            if (phase_inc != target_p_inc) {
                if (abs((int)phase_inc - (int)target_p_inc) < 1000000) phase_inc = target_p_inc;
                else if (phase_inc < target_p_inc) phase_inc += 500000;
                else phase_inc -= 500000;
            }
            phase += phase_inc;
            byte idx = (phase >> 24) & 0xFF;
            int s = 0;
            switch(current_waveform) {
                case 0: s = (int)sine_lut[idx] - 128; break;
                case 1: s = (int)idx - 128; break;
                case 2: s = (int)(idx < 128 ? idx * 2 : (255 - idx) * 2) - 128; break;
                case 3: s = (idx < 128 ? 127 : -128); break;
            }
            int raw = 128 + (((s * (int)(amplitude >> 16))) >> 16);
            int filtered = lpf_prev + ((lpf_alpha * (raw - lpf_prev)) >> 8);
            lpf_prev = filtered;
            *out++ = (filtered - 128) / 128.0f;
            if (amplitude > decay_rate) amplitude -= decay_rate;
            else { amplitude = 0; phase_inc = 0; target_p_inc = 0; }
        } else {
            *out++ = 0; phase_inc = 0; target_p_inc = 0; lpf_prev = 128;
        }
    }
    return paContinue;
}

// --- Evolution Trigger ---
void triggerEvolution(bool liked) {
    state_mutex.lock();
    if (liked) {
        for(int i=0; i<NUM_STEPS; i++) if (best_genome.gate[i]) note_bias[best_genome.note[i]]++;
        for(int g=0; g<20; g++) {
            mutatePopulation(population, next_gen, best_genome, h_state, note_bias, 0.15);
            for(int i=0; i<POP_SIZE; i++) population[i] = next_gen[i];
            int bs = -999999, bi = 0;
            for(int i=0; i<POP_SIZE; i++) { int s = evaluateGenome(population[i], h_state, note_bias); if(s>bs){bs=s; bi=i;}}
            best_genome = population[bi];
        }
    } else {
        initPopulation(population, h_state); for(int i=0; i<NUM_NOTES; i++) note_bias[i] = 0;
        int bs = -999999, bi = 0;
        for(int i=0; i<POP_SIZE; i++) { int s = evaluateGenome(population[i], h_state, note_bias); if(s>bs){bs=s; bi=i;}}
        best_genome = population[bi];
    }
    state_mutex.unlock();
}

// --- Sequencer Timer ---
Fl_Output *out_step;
void sequencer_timer_cb(void* v) {
    state_mutex.lock();
    byte note = best_genome.note[current_step], octave = best_genome.octave[current_step], gate = best_genome.gate[current_step];
    byte waveform = best_genome.waveform[current_step], tie = best_genome.tie[current_step], slide = best_genome.slide[current_step];
    int p_idx = (current_step == 0) ? NUM_STEPS - 1 : current_step - 1;
    byte pn = best_genome.note[p_idx], po = best_genome.octave[p_idx], pt = best_genome.tie[p_idx], ps = best_genome.slide[p_idx];

    int freq = freq_lut[note + octave * 12 + 24];
    uint32_t p_inc = (uint32_t)(((double)freq / 44100.0) * 4294967296.0);

    if (gate == 1) {
        bool is_t = (pt == 1 && note == pn && octave == po), is_s = (ps == 1);
        current_waveform = waveform; target_p_inc = p_inc;
        if (!is_s) phase_inc = p_inc;
        if (!is_t && !is_s) amplitude = 0xFFFFFFFF;
    } else { target_p_inc = 0; }

    static char buf[4]; sprintf(buf, "%d", current_step); out_step->value(buf);
    current_step = (current_step + 1) % NUM_STEPS;
    state_mutex.unlock();

    float step_sec = 60.0 / bpm / 4.0;
    decay_rate = 0xFFFFFFFF / (44100 * (step_sec * 1.5));
    Fl::repeat_timeout(step_sec, sequencer_timer_cb);
}

// --- UI Callbacks ---
void like_cb(Fl_Widget* w, void*) { triggerEvolution(true); }
void dislike_cb(Fl_Widget* w, void*) { triggerEvolution(false); }
void bpm_cb(Fl_Widget* w, void*) { bpm = ((Fl_Value_Slider*)w)->value(); }
void filter_cb(Fl_Widget* w, void*) { lpf_alpha = ((Fl_Value_Slider*)w)->value(); }
void scale_cb(Fl_Widget* w, void*) {
    state_mutex.lock();
    h_state.sequence[0].scale_idx = ((Fl_Choice*)w)->value();
    h_state.sequence_len = 1; // Simplified for GUI
    state_mutex.unlock();
    triggerEvolution(true);
}

int main() {
    srand(time(NULL));
    for (int i = 0; i < 128; i++) freq_lut[i] = (int)(440.0 * pow(2.0, (i - 69) / 12.0));
    for (int i = 0; i < 256; i++) sine_lut[i] = (byte)(128 + 127 * sin(i * 2.0 * M_PI / 256.0));
    h_state.sequence[0] = {0, 0}; h_state.sequence_len = 1;
    initPopulation(population, h_state); triggerEvolution(false);

    Pa_Initialize();
    PaStream *stream;
    Pa_OpenDefaultStream(&stream, 0, 1, paFloat32, 44100, 256, audioCallback, NULL);
    Pa_StartStream(stream);

    Fl_Window *win = new Fl_Window(450, 400, "ESP32 Genetic Bass Mock");
    Fl_Button *btn_like = new Fl_Button(50, 30, 150, 40, "LIKE (Evolve)");
    btn_like->callback(like_cb);
    Fl_Button *btn_dis = new Fl_Button(250, 30, 150, 40, "DISLIKE (Reset)");
    btn_dis->callback(dislike_cb);

    Fl_Value_Slider *sld_bpm = new Fl_Value_Slider(100, 100, 300, 25, "BPM");
    sld_bpm->type(FL_HOR_SLIDER); sld_bpm->range(60, 300); sld_bpm->value(120); sld_bpm->callback(bpm_cb);

    Fl_Value_Slider *sld_flt = new Fl_Value_Slider(100, 140, 300, 25, "Filter");
    sld_flt->type(FL_HOR_SLIDER); sld_flt->range(0, 256); sld_flt->value(256); sld_flt->callback(filter_cb);

    Fl_Choice *cho_scale = new Fl_Choice(100, 180, 150, 25, "Scale");
    for(int i=0; i<11; i++) cho_scale->add(SCALES[i].name);
    cho_scale->value(0); cho_scale->callback(scale_cb);

    out_step = new Fl_Output(100, 220, 50, 25, "Step");

    win->end();
    win->show();

    Fl::add_timeout(0.1, sequencer_timer_cb);
    return Fl::run();
}
