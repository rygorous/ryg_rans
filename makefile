all: main main64

main: main.cpp utils.h
	g++ -o main main.cpp -O3 -lm

main64: main64.cpp utils.h
	g++ -o main64 main64.cpp -O3 -lm

clean:
	rm -f main64 main
