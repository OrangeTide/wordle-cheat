CFLAGS += -Wall -W -O1 -ggdb
all :: wordle
wordle : LDFLAGS += -leditline
