

NAME = missile
CFLAGS := $(CFLAGS) -Wall -pedantic -std=c99
LDFLAGS := $(LDFLAGS) -lusb-1.0

$(NAME): $(NAME).c
	gcc $(CFLAGS) $(LDFLAGS) -o $@ $<
