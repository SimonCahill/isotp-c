include vars.mk

CFLAGS := -Wall -g -ggdb $(STD)
LDFLAGS := -shared
BIN := ./bin

.PHONY: all clean fPIC no_opt $(BIN)/$(LIB_NAME) $(BIN)/$(LIB_NAME).$(MAJOR_VER) $(BIN)/$(LIB_NAME).$(MAJOR_VER).$(MINOR_VER).$(REVISION) 

###
# BEGIN TARGETS
###

###
# Builds all library artifacts
###    
all: $(BIN)/$(LIB_NAME)
	@printf "########## BUILT $^ ##########\n\n\n"
	
fPIC: CFLAGS += "-fPIC"

###
# Builds all targets w/o optimisations enabled
###
no_opt: CFLAGS += -g -O0 all

###
# Removes all build artifacts
###
clean:
	-rm -f *.o $(BIN)/$(LIB_NAME)*

###
# Builds all library artifacts, including all symlinks.
###
$(BIN)/$(LIB_NAME): $(BIN)/$(LIB_NAME).$(MAJOR_VER)
	-ln -s $^ $@
	@printf "Linked $^ --> $@...\n"

###
# Builds the shared object along with one symlink
###
$(BIN)/$(LIB_NAME).$(MAJOR_VER): $(BIN)/$(LIB_NAME).$(MAJOR_VER).$(MINOR_VER).$(REVISION)
	-ln -s $^ $@
	@printf "Linked $^ --> $@...\n"

$(BIN)/$(LIB_NAME).$(MAJOR_VER).$(MINOR_VER).$(REVISION): $(BIN)/libisotp.o
	@if [ ! -d $(BIN) ]; then mkdir $(BIN); fi;
	${COMP} $^ -o $@ ${LDFLAGS}
	
###
# Compiles the isotp.c TU to an object file. 
###
$(BIN)/libisotp.o: isotp.c
	@mkdir -p $(BIN)
	${COMP} -c $^ -o $@ ${CFLAGS} -DISO_TP_FRAME_PADDING
	
install: all
	@printf "Installing $(LIB_NAME) to $(INSTALL_DIR)...\n"
	cp $(BIN)/$(LIB_NAME)* $(INSTALL_DIR)
	@printf "Library was installed...\n"

cmake:
	@mkdir -p build
	@cmake -B build -DCMAKE_BUILD_TYPE=Release
	@cmake --build build --config Release

###
# END TARGETS
###

