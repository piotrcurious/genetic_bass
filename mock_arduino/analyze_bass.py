import subprocess
import sys

# Scale degrees for the progression chords
# C major: [0, 2, 4, 5, 7, 9, 11] (Root: 0)
# A minor: [9, 11, 0, 2, 4, 5, 7] (Root: 9)
# G dom7: [7, 9, 11, 0, 2, 4, 5]  (Root: 7)
# D minor: [2, 4, 5, 7, 9, 10, 0] (Root: 2)
CHORDS = [
    [0, 2, 4, 5, 7, 9, 11],
    [9, 11, 0, 2, 4, 5, 7],
    [7, 9, 11, 0, 2, 4, 5],
    [2, 4, 5, 7, 9, 10, 0]
]

def freq_to_midi(f):
    if f <= 0: return None
    import math
    return round(12 * math.log2(f / 440.0) + 69)

def run_mock(iterations=100):
    result = subprocess.run(['./mock_arduino/mock', str(iterations)], capture_output=True, text=True)
    if result.returncode != 0:
        print("Error running mock:", result.stderr)
        return None
    try:
        frequencies = [int(f) for f in result.stdout.strip().split()]
        return frequencies
    except ValueError:
        print("Error parsing frequencies:", result.stdout)
        return None

def analyze_musicality(freqs):
    if not freqs: return

    midis = [freq_to_midi(f) for f in freqs]
    notes = [m % 12 if m is not None else None for m in midis]

    num_steps = len(notes)
    chord_tones = 0
    total_played = 0
    intervals = []
    prev_midi = None

    for i, note in enumerate(notes):
        if note is not None:
            total_played += 1
            chord_idx = i // (num_steps // 4)
            if note in CHORDS[chord_idx]:
                chord_tones += 1

            if prev_midi is not None:
                intervals.append(abs(midis[i] - prev_midi))
            prev_midi = midis[i]

    print(f"--- Musicality Analysis ---")
    print(f"Notes played: {total_played}/{num_steps}")
    if total_played > 0:
        print(f"Chord tone accuracy: {chord_tones/total_played:.2%}")
    if intervals:
        print(f"Average interval size: {sum(intervals)/len(intervals):.2f} semitones")
        print(f"Max interval: {max(intervals)} semitones")

if __name__ == "__main__":
    iter_count = 100
    if len(sys.argv) > 1:
        iter_count = int(sys.argv[1])

    freqs = run_mock(iter_count)
    if freqs:
        print(f"Generated frequencies ({len(freqs)} steps):")
        print(freqs)
        analyze_musicality(freqs)
