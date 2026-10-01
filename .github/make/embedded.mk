# Build the library through the project's native Makefile. The additional
# compile-only examples mirror examples/CMakeLists.txt and need no board SDK.
include Makefile

EXAMPLE_NAMES := polling streaming callbacks can_fd
EXAMPLE_OBJECTS := $(addprefix $(BIN)/examples/isotp_example_,$(addsuffix .o,$(EXAMPLE_NAMES)))
EXAMPLE_HEADERS := $(wildcard examples/*.h) $(PUBLIC_HEADERS)

.PHONY: embedded-examples
embedded-examples: $(EXAMPLE_OBJECTS)

$(BIN)/examples/isotp_example_polling.o: EXAMPLE_CPPFLAGS := -DISO_TP_MAX_CAN_FRAME_SIZE=8
$(BIN)/examples/isotp_example_streaming.o: EXAMPLE_CPPFLAGS := -DISO_TP_MAX_CAN_FRAME_SIZE=8 -DISO_TP_ENABLE_STREAMING
$(BIN)/examples/isotp_example_callbacks.o: EXAMPLE_CPPFLAGS := -DISO_TP_MAX_CAN_FRAME_SIZE=8 -DISO_TP_TRANSMIT_COMPLETE_CALLBACK -DISO_TP_RECEIVE_COMPLETE_CALLBACK
$(BIN)/examples/isotp_example_can_fd.o: EXAMPLE_CPPFLAGS := -DISO_TP_MAX_CAN_FRAME_SIZE=64 -DISO_TP_DEFAULT_TX_DL=64 -DISO_TP_USER_SEND_CAN_FLAGS -DISO_TP_CAN_FD_USE_BRS -DISO_TP_USER_SEND_CAN_ARG

# Examples have their own feature definitions, independent of the library's
# profile. FORCE also prevents reusing objects after compiler/flag changes.
$(BIN)/examples/%.o: examples/%.c $(EXAMPLE_HEADERS) FORCE
	mkdir -p $(BIN)/examples
	$(COMP) -c $< -o $@ -I. $(CPPFLAGS) $(EXAMPLE_CPPFLAGS) $(CFLAGS)
