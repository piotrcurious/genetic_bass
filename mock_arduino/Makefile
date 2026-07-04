CXX = g++
CXXFLAGS = -O2 -Wall -I. -I..

all: mock

mock: mock.cpp bass_logic.h ../genetic_bass_core.h
	$(CXX) $(CXXFLAGS) mock.cpp -o mock

clean:
	rm -f mock
