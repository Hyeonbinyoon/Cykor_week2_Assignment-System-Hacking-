MONGSHELL.out: mongshell.o tokenizer.o parser.o executer.o
	gcc -o MONGSHELL mongshell.o tokenizer.o parser.o executer.o

mongshell.o: mongshell.c tokenizer.h parser.h executer.h
	gcc -c mongshell.c

tokenizer.o: tokenizer.c tokenizer.h
	gcc -c tokenizer.c

parser.o: parser.c parser.h tokenizer.h
	gcc -c parser.c

executer.o: executer.c executer.h parser.h tokenizer.h
	gcc -c executer.c


