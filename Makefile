CC      = cc
CFLAGS  = -std=c11 -Wall -Wextra -Wpedantic -g
LIBS    = -lcurl
TARGET  = jibl
SRCDIR  = src
INCDIR  = include
OBJDIR  = obj

SRCS    = $(wildcard $(SRCDIR)/*.c)
OBJS    = $(patsubst $(SRCDIR)/%.c, $(OBJDIR)/%.o, $(SRCS))

.PHONY: all clean run emit clean-cache

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -I$(INCDIR) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

run: $(TARGET)
	./$(TARGET) examples/hello_en.jibl

emit: $(TARGET)
	./$(TARGET) --emit examples/hello_en.jibl

clean:
	rm -rf $(OBJDIR) $(TARGET)

clean-cache:
	rm -rf .jibl_cache
