CFLAGS=-Wall -Wextra
CXXFLAGS=$(CFLAGS)

parser: parser.o scanner.o ast.o
	$(CXX) -o $@ $^

clean:
	rm -f *.o parser
