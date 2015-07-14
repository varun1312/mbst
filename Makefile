all: clean comp run

debug: clean comp_debug run_debug

clean:
	rm -rf a.out

comp:
	g++ -pthread -std=c++11 nbBst.cpp

run:
	./a.out

comp_debug:
	g++ -g -pthread -std=c++11 nbBst.cpp

run_debug:
	gdb ./a.out

