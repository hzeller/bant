CFLAGS=-Wall -Wextra
CXXFLAGS=$(CFLAGS)

parser: parser.o scanner.o ast.o
	$(CXX) -o $@ $^

ast.o : ast.cc ast.h arena.h
parser.o: parser.cc scanner.h ast.h arena.h
scanner.o: scanner.h

clean:
	rm -f *.o parser
