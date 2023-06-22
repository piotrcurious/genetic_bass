// Arduino funky house bassline generator
// A0 pin controls BPM, the bassline is generated for 64 steps
// One digital pin outputs 4x4 beat trigger signal
// Bassline is output on pins 9 and 10, creating two separate stereo channels
// Allowing two note polyphony and PWM modulation is used for ADSR and volume
// Bassline generator algorithm uses jazz music theory convergence table
// And evolutionary algorithm to converge to elements highlighted by user
// By pressing buttons "like" and "dislike" representing momentary position of the sequencer

#include <MozziGuts.h>
#include <Oscil.h>
#include <tables/sin2048_int8.h>
#include <tables/saw8192_int8.h>
#include <tables/triangle8192_int8.h>
#include <tables/square8192_int8.h>
#include <ADSR.h>

#define BEAT_PIN 2 // digital pin for beat trigger signal
#define LIKE_PIN 3 // digital pin for like button
#define DISLIKE_PIN 4 // digital pin for dislike button

const int BPM_PIN = A0; // analog pin for BPM control
const int NUM_STEPS = 64; // number of steps in the bassline sequence
const int NUM_NOTES = 12; // number of notes in the chromatic scale
const int NUM_GENES = 4; // number of genes in the bassline genome

// define the waveforms for the bassline oscillators
Oscil<SIN2048_NUM_CELLS, AUDIO_RATE> osc1(SIN2048_DATA);
Oscil<SAW8192_NUM_CELLS, AUDIO_RATE> osc2(SAW8192_DATA);
Oscil<TRIANGLE8192_NUM_CELLS, AUDIO_RATE> osc3(TRIANGLE8192_DATA);
Oscil<SQUARE8192_NUM_CELLS, AUDIO_RATE> osc4(SQUARE8192_DATA);

// define the ADSR envelopes for the bassline oscillators
ADSR env1;
ADSR env2;

// define the bassline genome structure
struct Genome {
  byte note[NUM_STEPS]; // note values for each step (0-11)
  byte octave[NUM_STEPS]; // octave values for each step (0-3)
  byte waveform[NUM_STEPS]; // waveform values for each step (0-3)
  byte gate[NUM_STEPS]; // gate values for each step (0-1)
};

// define the jazz music theory convergence table
// each row represents a chord type (major, minor, dominant, etc.)
// each column represents a scale degree (root, third, fifth, etc.)
// each cell contains a score for how well the scale degree fits the chord type
const byte JAZZ_TABLE[7][7] = {
  {10, 0, 7, 0, 5, 0, 3}, // major chord
  {10, 0, 5, 0, 7, 0, 3}, // minor chord
  {10, 0, 7, 0, 5, -5, -10}, // dominant chord
  {10, -5, -10, -5, -10, -5, -10}, // diminished chord
  {10, -5, -10, -5, -10, -5, -10}, // half-diminished chord
  {10, -5, -10, -5, -10,-5,-10}, // augmented chord
  {10,-5,-10,-5,-10,-5,-10} // suspended chord
};

// define the chord progression to base the bassline generation on
// each element represents a chord type (0-6) and a root note (0-11)
const byte CHORD_PROGRESSION[4][2] = {
  {0,0}, // C major
  {1,9}, // A minor
  {2,7}, // G dominant
  {1,2} // D minor
};

// define the population size and mutation rate for the evolutionary algorithm
const int POP_SIZE = 100;
const float MUT_RATE = 0.01;

// declare global variables for the bassline generation and playback
Genome population[POP_SIZE]; // array of genomes
Genome best_genome; // best genome so far
int best_score; // best score so far
int current_step; // current step in the sequence
int current_bpm; // current BPM value
unsigned long step_time; // time interval for each step
unsigned long last_time; // last time a step was played
bool beat_on; // flag for beat signal
bool like_pressed; // flag for like button press
bool dislike_pressed; // flag for dislike button press

void setup() {
  startMozzi(); // start Mozzi library
  pinMode(BEAT_PIN, OUTPUT); // set beat pin as output
  pinMode(LIKE_PIN, INPUT_PULLUP); // set like pin as input with pullup resistor
  pinMode(DISLIKE_PIN, INPUT_PULLUP); // set dislike pin as input with pullup resistor
  randomSeed(analogRead(0)); // seed the random number generator with a random analog value
  initPopulation(); // initialize the population with random genomes
  evaluatePopulation(); // evaluate the population and find the best genome and score
  current_step = 0; // start from the first step
  current_bpm = analogRead(BPM_PIN) / 4 + 60; // read the BPM value from the analog pin and map it to 60-300 range
  step_time = 60000 / current_bpm / 4; // calculate the time interval for each step based on BPM and quarter notes
  last_time = millis(); // record the current time
  beat_on = false; // set the beat signal to off
  like_pressed = false; // set the like button press to false
  dislike_pressed = false; // set the dislike button press to false
}

void loop() {
  audioHook(); // update Mozzi sound engine
  
  unsigned long current_time = millis(); // get the current time
  
  if (current_time - last_time >= step_time) { // if the time interval for a step has passed
    
    last_time = current_time; // update the last time
    
    playStep(); // play the current step
    
    current_step++; // increment the current step
    
    if (current_step == NUM_STEPS) { // if the end of the sequence is reached
      
      current_step = 0; // reset the current step
      
      if (like_pressed) { // if the like button was pressed
        
        like_pressed = false; // reset the like button press
        
        mutatePopulation(); // mutate the population
        
        evaluatePopulation(); // evaluate the population and find the best genome and score
        
      } else if (dislike_pressed) { // if the dislike button was pressed
        
        dislike_pressed = false; // reset the dislike button press
        
        initPopulation(); // reinitialize the population with random genomes
        
        evaluatePopulation(); // evaluate the population and find the best genome and score
        
      }
      
    }
    
    beat_on = !beat_on; // toggle the beat signal
    
    digitalWrite(BEAT_PIN, beat_on ? HIGH : LOW); // output the beat signal to the digital pin
    
    current_bpm = analogRead(BPM_PIN) / 4 + 60; // read the BPM value from the analog pin and map it to 60-300 range
    
    step_time = 60000 / current_bpm / 4; // recalculate the time interval for each step based on BPM and quarter notes
    
  }
  
}

// function to initialize the population with random genomes
void initPopulation() {
  
  for (int i = 0; i < POP_SIZE; i++) { // for each genome in the population
    
    for (int j = 0; j < NUM_STEPS; j++) { // for each step in the genome
      
      population[i].note[j] = random(NUM_NOTES); // assign a random note value (0-11)
      
      population[i].octave[j] = random(4); // assign a random octave value (0-3)
      
      population[i].waveform[j] = random(4); // assign a random waveform value (0-3)
      
      population[i].gate[j] = random(2); // assign a random gate value (0-1)
      
    }
    
  }
  
}

// function to mutate the population based on mutation rate
void mutatePopulation() {
  
  for (int i = 0; i < POP_SIZE; i++) { // for each genome in the population
    
    for (int j = 0; j < NUM_STEPS; j++) { // for each step in the genome
      
      if (random(100) < MUT_RATE * 100) { // if a random number is less than mutation rate
        
        population[i].note[j] = random(NUM_NOTES); // assign a new random note value (0-11)
        
      }
      
      if (random(100) < MUT_RATE * 100) { // if a random number is less than mutation rate
        
        population[i].octave[j] = random(4); // assign a new random octave value (0-3)
        
      }
      
      if (random(100) < MUT_RATE * 100) { // if a random number is less than mutation rate
        
        population[i].waveform[j] = random(4); // assign a new random waveform value (0-3)
        
      }
      
      if (random(100) < MUT_RATE * 100) { // if a random number is less than mutation rate
        
        population[i].gate[j] = random(2); // assign a new random gate value (0-1)
        
      }
      
    }
    
  }
  
}

// function to evaluate the population and find the best genome and score
void evaluatePopulation() {
  
  best_score = -9999; // initialize the best score to a very low value
  
  for (int i = 0; i < POP_SIZE; i++) { // for each genome in the population
    
    int score = evaluateGenome(population[i]); // evaluate the genome and get its score
    
    if (score > best_score) { // if the score is better than the best score
      
      best_score = score; // update the best score
      
      best_genome = population[i]; // update the best genome
      
    }
    
  }
  
}

// function to evaluate a genome and return its score
int evaluateGenome(Genome genome) {
  
  int score = 0; // initialize the score to zero
  
  for (int i = 0; i < NUM_STEPS; i++) { // for each step in the genome
    
    byte note = genome.note[i]; // get the note value
    
    byte octave = genome.octave[i]; // get the octave value
    
    byte waveform = genome.waveform[i]; // get the waveform value
    
    byte gate = genome.gate[i]; // get the gate value
    
    byte chord_type = CHORD_PROGRESSION[i / (NUM_STEPS / 4)][0]; // get the chord type for this step
    
    byte chord_root = CHORD_PROGRESSION[i / (NUM_STEPS / 4)][1]; // get the chord root for this step
    
    byte scale_degree = (note - chord_root + NUM_NOTES) % NUM_NOTES; // calculate the scale degree for this note
    
    score += JAZZ_TABLE[chord_type][scale_degree]; // add the score from the jazz table based on chord type and scale degree
    
    score -= abs(octave - 1) * 2; // subtract some score based on octave deviation from 1
    
    score -= abs(waveform - 1) * 2; // subtract some score based on waveform deviation from 1 (sawtooth)
    
    if (i > 0) { // if not the first step
      
      byte prev_note = genome.note[i-1]; // get the previous note value
      
      byte prev_octave = genome.octave[i-1]; // get the previous octave value
      
      byte prev_gate = genome.gate[i-1]; // get the previous gate value
      
      int interval = note + octave * NUM_NOTES - prev_note - prev_octave * NUM_NOTES; // calculate the interval between this note and previous note
      
      score -= abs(interval); // subtract some score based on interval magnitude
      
      if (interval == 0 && gate == 1 && prev_gate == 1) { // if the same note is repeated and both gates are on
        
        score -= 5; // subtract some score for lack of variation
        
      }
      
    }
    
  }
  
  return score; // return the final score
  
}

// function to play a step in the sequence
void playStep() {
  
  byte note = best_genome.note[current_step]; // get the note value
  
  byte octave = best_genome.octave[current_step]; // get the octave value
  
  byte waveform = best_genome.waveform[current_step]; // get the waveform value
  
  byte gate = best_genome.gate[current_step]; // get the gate value
  
  int freq = mtof(note + octave * NUM_NOTES); // calculate the frequency based on note and octave values
  
  switch (waveform) { // switch on waveform value
    
    case 0: // sine wave
      osc1.setFreq(freq); // set frequency for osc1
      osc2.setFreq(freq); // set frequency for osc2
      break;
      
    case 1: // sawtooth wave
      osc3.setFreq(freq); // set frequency for osc3
      osc4.setFreq(freq); // set frequency for osc4
      break;
      
    case 2: // triangle wave
      osc1.setFreq(freq); // set frequency for osc1
      osc4.setFreq(freq); // set frequency for osc4
      break;
      
    case 3: // square wave
      osc2.setFreq(freq); // set frequency for osc2
      osc3.setFreq(freq); // set frequency for osc3
      break;
      
  }
  
  if (gate == 1) { // if gate is on
    
    env1.noteOn(); // trigger envelope for env1
    
    env2.noteOn(); // trigger envelope for env2
    
  } else { // if gate is off
    
    env1.noteOff(); // release envelope for env1
    
    env2.noteOff(); // release envelope for env2
    
  }
  
}

// function to convert a MIDI note value to frequency
int mtof(byte note) {
  
  return 440 * pow(2, (note - 69) / 12.0); // use the formula f = 440 * 2^((n-69)/12)
  
}

// function to generate the audio output
int updateAudio() {
  
  int out = 0; // initialize the output to zero
  
  out += (osc1.next() + osc2.next()) * env1.next() / 4; // add the output of osc1 and osc2 modulated by env1
  
  out += (osc3.next() + osc4.next()) * env2.next() / 4; // add the output of osc3 and osc4 modulated by env2
  
  return out; // return the output
  
}

// function to handle button presses
void updateControl() {
  
  if (digitalRead(LIKE_PIN) == LOW) { // if the like button is pressed
    
    like_pressed = true; // set the like button press to true
    
  }
  
  if (digitalRead(DISLIKE_PIN) == LOW) { // if the dislike button is pressed
    
    dislike_pressed = true; // set the dislike button press to true
    
  }
  
}
