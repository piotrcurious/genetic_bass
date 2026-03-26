import subprocess
import sys

# Scale degrees for the progression chords
PROGRESSIONS = [
    [[0, 2, 4, 5, 7, 9, 11], [9, 11, 0, 2, 4, 5, 7], [7, 9, 11, 0, 2, 4, 5], [2, 4, 5, 7, 9, 10, 0]], # House
    [[0, 2, 4, 5, 7, 9, 10], [5, 7, 9, 10, 0, 2, 4], [0, 2, 4, 5, 7, 9, 10], [7, 9, 11, 0, 2, 4, 5]], # Blues
    [[2, 4, 5, 7, 9, 10, 0], [7, 9, 11, 0, 2, 4, 5], [0, 2, 4, 5, 7, 9, 11], [0, 2, 4, 5, 7, 9, 11]], # Jazz II-V-I
    [[4, 6, 7, 9, 11, 1, 2], [4, 6, 7, 9, 11, 1, 2], [9, 11, 1, 2, 4, 6, 7], [9, 11, 1, 2, 4, 6, 7]], # Funk (Em7, A7)
    [[0, 2, 4, 5, 7, 9, 11], [7, 9, 11, 0, 2, 4, 5], [9, 11, 0, 2, 4, 5, 7], [5, 7, 9, 11, 0, 2, 4]], # Pop (C, G, Am, F)
    [[0, 2, 3, 5, 7, 8, 10], [5, 7, 8, 10, 0, 2, 3], [0, 2, 3, 5, 7, 8, 10], [7, 8, 10, 0, 2, 3, 5]], # Minor Blues
    [[2, 4, 6, 7, 9, 11, 1], [9, 11, 1, 2, 4, 6, 7], [4, 6, 8, 9, 11, 1, 3], [7, 9, 11, 0, 2, 4, 6]]  # Rock (D, A, E, G)
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
            chord_idx = i // (num_steps // 4)
            if note in PROGRESSIONS[progression_idx][chord_idx]:
                chord_tones += 1
    return chord_tones/total_played if total_played > 0 else 0

if __name__ == "__main__":
    print("Testing command sequence: reset -> iters 100 -> save -> load -> iters 100")
    # Reset and run 100 generations
    freqs1 = run_mock(["reset", "iters", "100", "save"])
    acc1 = get_musicality_score(freqs1, 0)
    print(f"Initial Acc: {acc1:.2%}")

    # Load and run another 100 generations
    freqs2 = run_mock(["load", "iters", "100"])
    acc2 = get_musicality_score(freqs2, 0)
    print(f"Post-load Acc: {acc2:.2%}")

    # Switch progression and reset
    freqs3 = run_mock(["prog", "3", "iters", "200"])
    acc3 = get_musicality_score(freqs3, 3)
    print(f"Prog 3 Acc: {acc3:.2%}")
