import subprocess
import sys

# Scale masks
SCALES = [
    ("Major", 0b101010110101), ("Minor", 0b010110101101), ("Dorian", 0b011010101101),
    ("Phrygian", 0b010110101011), ("Lydian", 0b101010100101), ("Mixolydian", 0b011010110101),
    ("Locrian", 0b010101101011), ("Pentatonic Maj", 0b101000110101), ("Pentatonic Min", 0b010100101001),
    ("Blues", 0b010101101001), ("Chromatic", 0b111111111111)
]

# Default Sequence used in mock
DEFAULT_SEQ = [(0, 0), (5, 0), (7, 2), (0, 0)]

def freq_to_midi(f_str):
    is_tie = f_str.startswith('T')
    is_slide = f_str.startswith('S')
    prefix_len = 1 if (is_tie or is_slide) else 0
    f_val = int(f_str[prefix_len:])
    if f_val <= 0: return None, is_tie, is_slide
    import math
    return round(12 * math.log2(f_val / 440.0) + 69), is_tie, is_slide

def run_mock(commands=[]):
    result = subprocess.run(['./mock_arduino/mock'] + commands, capture_output=True, text=True)
    if result.returncode != 0: return None
    return result.stdout.strip().split()

def analyze_sequence_adherence(freq_strs):
    num_steps = len(freq_strs)
    in_scale = 0
    total = 0
    for i, f_str in enumerate(freq_strs):
        midi, _, _ = freq_to_midi(f_str)
        if midi is not None:
            total += 1
            note = midi % 12
            chord_idx = (i * 4) // num_steps
            root, scale_idx = DEFAULT_SEQ[chord_idx]
            mask = SCALES[scale_idx][1]
            rel = (note - root + 12) % 12
            if (mask >> rel) & 1: in_scale += 1
    return in_scale/total if total > 0 else 0

if __name__ == "__main__":
    print("Testing Theory-Driven Sequence Adherence...")
    freqs = run_mock()
    if freqs:
        adh = analyze_sequence_adherence(freqs)
        print(f"Overall Chord/Scale Adherence: {adh:.1%}")

        slides = sum(1 for f in freqs if f.startswith('S'))
        ties = sum(1 for f in freqs if f.startswith('T'))
        print(f"Rhythmic variety: {slides} slides, {ties} ties.")
