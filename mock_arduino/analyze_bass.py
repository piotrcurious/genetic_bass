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

def run_mock(iterations=500):
    result = subprocess.run(['./mock_arduino/mock', 'iters', str(iterations)], capture_output=True, text=True)
    if result.returncode != 0: return None
    return result.stdout.strip().split()

def analyze_output(freq_strs, expected_sequence):
    num_steps = len(freq_strs)
    in_scale = 0
    total_played = 0
    unique_steps = set()
    slides = 0
    ties = 0
    off_beat_notes = 0
    repeats = 0

    for i, f_str in enumerate(freq_strs):
        midi, tie, slide = freq_to_midi(f_str)
        if slide: slides += 1
        if tie: ties += 1

        if midi is not None:
            total_played += 1
            if i % 2 == 1: off_beat_notes += 1

            note = midi % 12
            chord_idx = (i * len(expected_sequence)) // num_steps
            root, scale_idx = expected_sequence[chord_idx]
            mask = SCALES[scale_idx][1]
            rel = (note - root + 12) % 12
            if (mask >> rel) & 1: in_scale += 1
            unique_steps.add(f_str)

    # Check for 16-step functional repeats
    for b in range(num_steps // 16 - 1):
        ci_b = (b * 16 * len(expected_sequence)) // num_steps
        root_b = expected_sequence[ci_b][0]
        for b2 in range(b + 1, num_steps // 16):
            ci_b2 = (b2 * 16 * len(expected_sequence)) // num_steps
            root_b2 = expected_sequence[ci_b2][0]

            matches = 0
            for s in range(16):
                m_b, t_b, s_b = freq_to_midi(freq_strs[b*16+s])
                m_b2, t_b2, s_b2 = freq_to_midi(freq_strs[b2*16+s])

                if m_b is None and m_b2 is None:
                    matches += 1
                elif m_b is not None and m_b2 is not None:
                    if (m_b - root_b) % 12 == (m_b2 - root_b2) % 12:
                        matches += 1
            if matches >= 10:
                repeats += 1

    accuracy = (in_scale / total_played) * 100 if total_played > 0 else 0
    syncopation = (off_beat_notes / total_played) * 100 if total_played > 0 else 0
    diversity = (len(unique_steps) / num_steps) * 100

    return accuracy, diversity, syncopation, repeats, slides, ties, total_played

if __name__ == "__main__":
    current_mock_sequence = [(0, 0), (5, 0), (0, 0), (7, 0)]

    print(f"{'Run':<4} | {'Acc%':<5} | {'Div%':<5} | {'Sync%':<5} | {'Rpt':<3} | {'Sld':<3} | {'Tie':<3} | {'Notes'}")
    print("-" * 65)

    for run in range(10):
        freqs = run_mock(5000)
        if freqs:
            acc, div, sync, rpt, sld, tie, n = analyze_output(freqs, current_mock_sequence)
            print(f"{run+1:<4} | {acc:<5.1f} | {div:<5.1f} | {sync:<5.1f} | {rpt:<3} | {sld:<3} | {tie:<3} | {n}")
