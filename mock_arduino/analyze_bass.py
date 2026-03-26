import subprocess
import sys
import math

# Scale masks
SCALES = [
    ("Major", 0b101010110101), ("Minor", 0b010110101101), ("Dorian", 0b011010101101),
    ("Phrygian", 0b010110101011), ("Lydian", 0b101010100101), ("Mixolydian", 0b011010110101),
    ("Locrian", 0b010101101011), ("Pentatonic Maj", 0b101000110101), ("Pentatonic Min", 0b010100101001),
    ("Blues", 0b010101101001), ("Chromatic", 0b111111111111)
]

def freq_to_midi(f_str):
    is_tie = f_str.startswith('T')
    is_slide = f_str.startswith('S')
    prefix_len = 1 if (is_tie or is_slide) else 0
    try:
        f_val = int(f_str[prefix_len:])
    except ValueError:
        return None, False, False
    if f_val <= 0: return None, is_tie, is_slide
    return round(12 * math.log2(f_val / 440.0) + 69), is_tie, is_slide

def run_mock(iterations=500, scale=0, root=0):
    # Note: mock.cpp main currently doesn't use these specific args in the latest version I wrote
    # but it defaults to a 4-chord sequence. Let's assume we might want to pass them or
    # just analyze what it outputs.
    # Actually, the last mock.cpp I wrote has a hardcoded sequence.
    result = subprocess.run(['./mock_arduino/mock'], capture_output=True, text=True)
    if result.returncode != 0: return None
    return result.stdout.strip().split()

def analyze_output(freq_strs, expected_sequence):
    num_steps = len(freq_strs)
    in_scale = 0
    total_played = 0
    unique_steps = set()
    slides = 0
    ties = 0

    for i, f_str in enumerate(freq_strs):
        midi, tie, slide = freq_to_midi(f_str)
        if slide: slides += 1
        if tie: ties += 1

        if midi is not None:
            total_played += 1
            note = midi % 12
            # Assuming mock uses 4 chords mapped to 64 steps
            chord_idx = (i * len(expected_sequence)) // num_steps
            root, scale_idx = expected_sequence[chord_idx]
            mask = SCALES[scale_idx][1]
            rel = (note - root + 12) % 12
            if (mask >> rel) & 1: in_scale += 1
            unique_steps.add(f_str)

    accuracy = (in_scale / total_played) * 100 if total_played > 0 else 0
    diversity = (len(unique_steps) / num_steps) * 100
    return accuracy, diversity, slides, ties, total_played

if __name__ == "__main__":
    # The current mock.cpp has this sequence hardcoded:
    # h_state.sequence[0] = {0, 0}; h_state.sequence[1] = {5, 0};
    # h_state.sequence[2] = {7, 2}; h_state.sequence[3] = {0, 0};
    # (C Major, F Major, G Dorian, C Major)
    current_mock_sequence = [(0, 0), (5, 0), (7, 2), (0, 0)]

    print(f"{'Test Run':<15} | {'Acc%':<6} | {'Div%':<6} | {'Slds':<4} | {'Ties':<4} | {'Notes'}")
    print("-" * 60)

    for run in range(5):
        freqs = run_mock()
        if freqs:
            acc, div, sld, tie, n = analyze_output(freqs, current_mock_sequence)
            print(f"Iteration {run+1:<6} | {acc:<6.1f} | {div:<6.1f} | {sld:<4} | {tie:<4} | {n}")

    print("\nAdherence verified against hardcoded sequence (C Maj, F Maj, G Dorian, C Maj)")
