CXX = g++
CXXFLAGS = -O2 -Wall `fltk-config --cxxflags`
LDFLAGS = `fltk-config --ldflags` -lportaudio

all: mock gui_app

mock: mock.cpp bass_logic.h
	$(CXX) $(CXXFLAGS) mock.cpp -o mock

gui_app: gui_app.cpp bass_logic.h
	$(CXX) $(CXXFLAGS) gui_app.cpp $(LDFLAGS) -o gui_app

clean:
	rm -f mock gui_app
