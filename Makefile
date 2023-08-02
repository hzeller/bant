CFLAGS ?= -Wall -Wextra -pedantic
CXXFLAGS=-std=c++17 $(CFLAGS)


parser: parser.o scanner.o ast.o
	$(CXX) -o $@ $^

ast.o : ast.cc ast.h arena.h
parser.o: parser.cc parser.h scanner.h ast.h arena.h
scanner.o: scanner.cc scanner.h

clean:
	rm -f *.o parser
