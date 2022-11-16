CFLAGS += -Wall -W -O1 -ggdb
all :: wordle
clean :: ; $(RM) wordle wordlist.c wordlist.h
wordle : wordle.c wordlist.c | wordlist.h
wordle : LDFLAGS += -leditline
wordlist.c wordlist.h : wordlist.txt ; ./genwordlist.sh wordlist.txt > wordlist.c
