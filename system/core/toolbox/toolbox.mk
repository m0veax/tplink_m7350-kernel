vpath %.c $(SRCDIR)/toolbox
vpath %.h $(SRCDIR)/toolbox

TOOLS := \
	ls \

TOOL_SRCLIST:= \
	$(patsubst %,%.c,$(TOOLS)) \
	toolbox.c 

TOOL_OBJLIST := $(TOOL_SRCLIST:%.c=%.o)

all: $(SRCDIR)/toolbox/tools.h $(SRCDIR)/toolbox/toolbox

$(SRCDIR)/toolbox/toolbox:$(TOOL_OBJLIST) 
	$(CC) $^ -o $@
	for t in $(TOOLS); do ln -sf toolbox $$t ; done
%.o:%.c
	$(CC) -c $^ -o $@ -I../include

$(SRCDIR)/toolbox/tools.h:
	@echo "/* file generated automatically */" > $@ ; for t in $(TOOLS) ; do echo "TOOL($$t)" >> $@ ; done

clean:
	rm -rf *.o $(TOOLS) tools.h toolbox
