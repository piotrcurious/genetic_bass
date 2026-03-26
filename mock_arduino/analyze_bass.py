import subprocess
import sys

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

if __name__ == "__main__":
    iter_count = 100
    if len(sys.argv) > 1:
        iter_count = int(sys.argv[1])

    freqs = run_mock(iter_count)
    if freqs:
        print(f"Generated frequencies ({len(freqs)} steps):")
        print(freqs)
