CXX = g++

CFLAGS = `pkg-config gstreamer-1.0 gstreamer-app-1.0  --cflags` -IFlyCapture
LIBS = `pkg-config gstreamer-1.0 gstreamer-app-1.0 --libs` -lflycapture

build: gst-app-src.cpp
	$(CXX) $(CFLAGS) gst-app-src.cpp $(LIBS) -o gst-app-src
	
clean:
	rm gst-app-src
