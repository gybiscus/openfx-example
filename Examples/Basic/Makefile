CXXFLAGS = -I../../include
OPTIMIZER = -g

basic.ofx : basic.o
	$(CXX) -bundle basic.o -o basic.ofx
	mkdir -p basic.ofx.bundle/Contents/MacOS/
	mv basic.ofx basic.ofx.bundle/Contents/MacOS/
