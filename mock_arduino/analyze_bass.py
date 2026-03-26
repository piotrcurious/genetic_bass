import subprocess
import sys

# Scale degrees for 16 styles
PROGRESSIONS = [
    [[0, 2, 4, 5, 7, 9, 11], [9, 11, 0, 2, 4, 5, 7], [7, 9, 11, 0, 2, 4, 5], [2, 4, 5, 7, 9, 10, 0]], # House
    [[0, 2, 4, 5, 7, 9, 10], [5, 7, 9, 10, 0, 2, 4], [0, 2, 4, 5, 7, 9, 10], [7, 9, 11, 0, 2, 4, 5]], # Blues
    [[2, 4, 5, 7, 9, 10, 0], [7, 9, 11, 0, 2, 4, 5], [0, 2, 4, 5, 7, 9, 11], [0, 2, 4, 5, 7, 9, 11]], # Jazz
    [[4, 6, 7, 9, 11, 1, 2], [4, 6, 7, 9, 11, 1, 2], [9, 11, 1, 2, 4, 6, 7], [9, 11, 1, 2, 4, 6, 7]], # Funk
    [[0, 2, 4, 5, 7, 9, 11], [7, 9, 11, 0, 2, 4, 5], [9, 11, 0, 2, 4, 5, 7], [5, 7, 9, 11, 0, 2, 4]], # Pop
    [[0, 2, 3, 5, 7, 8, 10], [5, 7, 8, 10, 0, 2, 3], [0, 2, 3, 5, 7, 8, 10], [7, 8, 10, 0, 2, 3, 5]], # Minor Blues
    [[2, 4, 6, 7, 9, 11, 1], [9, 11, 1, 2, 4, 6, 7], [4, 6, 8, 9, 11, 1, 3], [7, 9, 11, 0, 2, 4, 6]], # Rock
    [[9, 11, 0, 2, 4, 5, 7]] * 4, # Techno
    [[2, 4, 5, 7, 9, 10, 0]] * 4, # Psytrance
    [[0, 1, 3, 5, 7, 8, 10]] * 4, # Trap
    [[7, 9, 10, 0, 2, 3, 5]] * 4, # Reggae
    [[0, 2, 4, 5, 7, 9, 11]] * 4, # Bossa
    [[4, 6, 7, 9, 11, 1, 3]] * 4, # Synthwave
    [[9, 11, 0, 2, 4, 5, 7]] * 4, # Disco
    [[4, 6, 7, 9, 11, 1, 2]] * 4, # Metal
    [[0, 2, 4, 5, 7, 9, 11]] * 4  # Ambient
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

def analyze_rhythm(freq_strs):
    notes = 0
    ties = 0
    rests = 0
    for f in freq_strs:
        if f == "0": rests += 1
        elif f.startswith("T"): ties += 1
        else: notes += 1
    total = len(freq_strs)
    return notes/total, ties/total, rests/total

def get_musicality_score(freq_strs, progression_idx):
    midis = []
    for f in freq_strs:
        m, _ = freq_to_midi(f)
        midis.append(m)
    num_steps = len(midis)
    chord_tones = 0
    total_played = 0
    for i, midi in enumerate(midis):
        if midi is not None:
            total_played += 1
            note = midi % 12
            chord_idx = min(3, i // (num_steps // 4))
            if note in PROGRESSIONS[progression_idx][chord_idx]:
                chord_tones += 1
    return chord_tones/total_played if total_played > 0 else 0

if __name__ == "__main__":
    print(f"{'Style':<5} | {'Acc':<8} | {'Note%':<7} | {'Tie%':<7} | {'Rest%':<7}")
    print("-" * 50)
    for p in range(16):
        freqs = run_mock(["prog", str(p), "iters", "300"])
        if freqs:
            acc = get_musicality_score(freqs, p)
            n, t, r = analyze_rhythm(freqs)
            print(f"{p:<5} | {acc:<8.1%} | {n:<7.1%} | {t:<7.1%} | {r:<7.1%}")
