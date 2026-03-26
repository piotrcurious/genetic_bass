import subprocess
import sys

# Scale degrees for the progression chords
PROGRESSIONS = [
    [ # House
        [0, 2, 4, 5, 7, 9, 11], [9, 11, 0, 2, 4, 5, 7], [7, 9, 11, 0, 2, 4, 5], [2, 4, 5, 7, 9, 10, 0]
    ],
    [ # Blues (C7, F7, C7, G7)
        [0, 2, 4, 5, 7, 9, 10], [5, 7, 9, 10, 0, 2, 4], [0, 2, 4, 5, 7, 9, 10], [7, 9, 11, 0, 2, 4, 5]
    ],
    [ # Jazz II-V-I
        [2, 4, 5, 7, 9, 10, 0], [7, 9, 11, 0, 2, 4, 5], [0, 2, 4, 5, 7, 9, 11], [0, 2, 4, 5, 7, 9, 11]
    ]
]

def freq_to_midi(f_str):
    is_tie = f_str.startswith('T')
    f_val = int(f_str[1:] if is_tie else f_str)
    if f_val <= 0: return None, is_tie
    import math
    return round(12 * math.log2(f_val / 440.0) + 69), is_tie

def run_mock(iterations=100, progression=0, likes=0):
    result = subprocess.run(['./mock_arduino/mock', str(iterations), str(progression), str(likes)], capture_output=True, text=True)
    if result.returncode != 0: return None
    return result.stdout.strip().split()

def get_musicality_score(freq_strs, progression_idx):
    midis = []
    ties = []
    for f in freq_strs:
        m, t = freq_to_midi(f)
        midis.append(m)
        ties.append(t)

    num_steps = len(midis)
    chord_tones = 0
    total_played = 0
    valid_ties = 0

    for i, midi in enumerate(midis):
        if midi is not None:
            total_played += 1
            note = midi % 12
            chord_idx = i // (num_steps // 4)
            if note in PROGRESSIONS[progression_idx][chord_idx]:
                chord_tones += 1
            if ties[i]:
                valid_ties += 1

    accuracy = chord_tones/total_played if total_played > 0 else 0
    tie_ratio = valid_ties/total_played if total_played > 0 else 0
    return accuracy, tie_ratio

if __name__ == "__main__":
    prog_idx = 0
    if len(sys.argv) > 1: prog_idx = int(sys.argv[1])

    print(f"Tracking Convergence and User Bias for Progression {prog_idx}...")
    print(f"{'Likes':<6} | {'Iters':<6} | {'Accuracy':<10} | {'Tie Ratio':<10}")
    print("-" * 45)
    for likes in [0, 2, 5]:
        for iters in [10, 100, 500]:
            freqs = run_mock(iters, prog_idx, likes)
            if freqs:
                acc, ties = get_musicality_score(freqs, prog_idx)
                print(f"{likes:<6} | {iters:<6} | {acc:<10.2%} | {ties:<10.2%}")
