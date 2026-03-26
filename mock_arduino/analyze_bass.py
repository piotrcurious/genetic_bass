import subprocess
import sys

# Scale masks
SCALES = [
    ("Major", 0b101010110101), ("Minor", 0b010110101101), ("Dorian", 0b011010101101),
    ("Phrygian", 0b010110101011), ("Lydian", 0b101010100101), ("Mixolydian", 0b011010110101),
    ("Locrian", 0b010101101011), ("Pentatonic Maj", 0b101000110101), ("Pentatonic Min", 0b010100101001),
    ("Blues", 0b010101101001), ("Chromatic", 0b111111111111)
]

def freq_to_midi(f_str):
    is_tie = f_str.startswith('T')
    f_val = int(f_str[1:] if is_tie else f_str)
    if f_val <= 0: return None, is_tie
    import math
    return round(12 * math.log2(f_val / 440.0) + 69), is_tie

def run_mock(commands):
    result = subprocess.run(['./mock_arduino/mock'] + commands, capture_output=True, text=True)
    if result.returncode != 0: return None
    return result.stdout.strip().split()

def analyze_scale_adherence(freq_strs, scale_idx, root):
    mask = SCALES[scale_idx][1]
    in_scale = 0
    total = 0
    unique_notes = set()
    for f in freq_strs:
        midi, _ = freq_to_midi(f)
        if midi is not None:
            total += 1
            note = midi % 12
            unique_notes.add(note)
            rel = (note - root + 12) % 12
            if (mask >> rel) & 1: in_scale += 1
    return in_scale/total if total > 0 else 0, len(unique_notes)

if __name__ == "__main__":
    print(f"{'Scale':<15} | {'Root':<4} | {'Adherence':<10} | {'Diversity'}")
    print("-" * 50)
    for i, (name, _) in enumerate(SCALES):
        for root in [0, 5]: # C and F
            freqs = run_mock(["scale", str(i), "root", str(root)])
            if freqs:
                adh, div = analyze_scale_adherence(freqs, i, root)
                print(f"{name:<15} | {root:<4} | {adh:<10.1%} | {div} notes")
