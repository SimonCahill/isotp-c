ISO-TP (ISO 15765-2) Support Library in C
================================

**This project is inspired by [openxc isotp-c](https://github.com/openxc/isotp-c), but the code has been completely re-written.**

This is a platform agnostic C library that implements the [ISO 15765-2](https://en.wikipedia.org/wiki/ISO_15765-2) (also known as ISO-TP) protocol, which runs over a CAN bus. Quoting Wikipedia:

>ISO 15765-2, or ISO-TP, is an international standard for sending data packets over a CAN-Bus.
>The protocol allows for the transport of messages that exceed the eight byte maximum payload of CAN frames. 
>ISO-TP segments longer messages into multiple frames, adding metadata that allows the interpretation of individual frames and reassembly 
>into a complete message packet by the recipient. It can carry up to 4095 bytes of payload per message packet.

This library doesn't assume anything about the source of the ISO-TP messages or the underlying interface to CAN. It uses dependency injection to give you complete control.

**The current version supports [ISO-15765-2](https://en.wikipedia.org/wiki/ISO_15765-2) single and multiple frame transmition, and works in Full-duplex mode.**

**CAN FD frames (up to 64 bytes per frame) are supported as of version 1.7.0; see [CAN FD support](#can-fd-support).**

## Builds

[![CI](https://github.com/SimonCahill/isotp-c/actions/workflows/ci.yml/badge.svg)](https://github.com/SimonCahill/isotp-c/actions/workflows/ci.yml)

## Contributors

It's at this point where I'd like to point out all the fantastic contributions made to this fork by the amazing people using it!
[List of contributors](https://github.com/SimonCahill/isotp-c/blob/master/CONTRIBUTORS.md)

Thank you all!

## Building ISOTP-C

This library may be built using either straight Makefiles, or using CMake.

### make
To build this library using Make, simply call:

```bash
$ make all
```

### CMake

The CMake build system allows for more flexibility at generation and build time, so it is recommended you use this for building this library.  
Of course, if your project does not use CMake, you don't *have* to use it.
If your projects use a different build system, you are more than welcome to include it in this repository.

The Makefile generator for isotpc will automatically detect whether or not your build system is using the `Debug` or `Release` build type and will adjust compiler parameters accordingly.

#### Unit tests

The unit suite includes mocked CAN transmission, timing, debugging, and
receive-callback shims. Enable it with CMake and run it through CTest:

```bash
cmake -S . -B build -Disotpc_ENABLE_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The streaming test subject enables streaming and receive callbacks
independently of the options used for the normal library target, but follows the
configured frame size, so that streaming is also covered on CAN FD builds.

The framing suite next to it is compiled once per configuration — Classical CAN
and CAN FD, with and without frame padding, as well as with the optional
callbacks, the additional `isotp_user_send_can` argument, the frame flags and
streaming — and covers segmentation, reassembly, chunked reception and the
handling of malformed frames.

#### Debug Build
If your project is configured to build as `Debug`, then the library will be compiled with **no** optimisations and **with** debug symbols.  
`-DCMAKE_BUILD_TYPE=Debug`

#### Release Build
If your project is configured to build as `Release`, then the library code will be **optimised** using `-O2` and will be **stripped**.  
`-DCMAKE_BUILD_TYPE=Release`

#### External Include Directories
It is generally considered good practice to segregate header files from each other, depending on the project. For this reason, you may opt in to this behaviour for this library.  

If you pass `-Disotpc_USE_INCLUDE_DIR=ON` on the command-line, or you set `set(isotpc_USE_INCLUDE_DIR ON CACHE BOOL "Use external include dir for isotp-c")` in your CMakeLists.txt, then a separate `include/` directory will
be added to the project.  
This happens at generation time, and the CMake project will automatically reference `${CMAKE_CURRENT_BINARY_DIR}/include` as the include directory for the project. This will be propagated to your projects, too.

In your code:

```c
// if -Disotpc_USE_INCLUDE_DIR=ON
#include <isotp/isotp.h>

// else
#include <isotp.h>
```

#### Static Library
In some cases, it is required that a static library be used instead of a shared library.
isotp-c supports this also, via options.

> ![NOTE] This option is enabled by default when building using MSVC.

Either pass `-Disotpc_STATIC_LIBRARY=ON` via command-line or `set(isotpc_STATIC_LIBRARY ON CACHE BOOL "Enable static library for isotp-c")` in your CMakeLists.txt and the library will be built as a static library (`*.a|*.lib`) for your project to include.

#### CAN FD support

By default the library only emits Classical CAN frames of up to 8 bytes. CAN FD support is enabled by raising the maximum CAN frame data length (CAN_DL) the library is compiled for:

```bash
# 8 (default, Classical CAN) or one of 12, 16, 20, 24, 32, 48, 64
$ cmake -B build -Disotpc_MAX_CAN_FRAME_SIZE=64
```

The same value may be set when building with Make (`make MAX_CAN_FRAME_SIZE=64 all`), or by defining `ISO_TP_MAX_CAN_FRAME_SIZE` in `isotp_config.h` if you compile the sources yourself.

This value determines the size of the internal frame buffers and therefore the largest frame the library is able to send and receive. Frames larger than the configured maximum are ignored on reception.

Once enabled, the following ISO 15765-2:2016 behaviour applies:

* Single frames carrying more than 7 bytes use the `SF_DL` escape sequence, which allows up to `TX_DL - 2` bytes (62 bytes at a `TX_DL` of 64) to be transmitted in one frame.
* First frames are always transmitted using the full frame length, and consecutive frames carry up to `TX_DL - 1` bytes.
* Frames larger than 8 bytes are always padded up to the next transmittable CAN FD length (8, 12, 16, 20, 24, 32, 48, 64) using `ISO_TP_FRAME_PADDING_VALUE`, since CAN FD only supports these discrete lengths. Frames of up to 8 bytes are only padded when `ISO_TP_FRAME_PADDING` is enabled.
* Reception adapts to the sender: the frame length of an incoming first frame defines the length expected from the following consecutive frames (`RX_DL`), so a CAN FD link happily receives Classical CAN messages, too.

The frame length used for transmission (`TX_DL`) is a per-link property, which defaults to `ISO_TP_DEFAULT_TX_DL` (the configured maximum) and may be lowered at runtime — useful when talking to a peer which is limited to Classical CAN frames:

```c
isotp_init_link(&link, 0x7TT, sendbuf, sizeof(sendbuf), recvbuf, sizeof(recvbuf));

/* transmit Classical CAN frames on this link, even though the library supports CAN FD */
if (ISOTP_RET_OK != isotp_set_tx_dl(&link, 8)) {
    /* the requested length is not a valid CAN_DL, or exceeds ISO_TP_MAX_CAN_FRAME_SIZE */
}
```

Note that `isotp_user_send_can` is handed frames of up to `TX_DL` bytes once CAN FD is enabled, so the CAN driver behind it must be set up to send CAN FD frames.

CAN FD combines with the [streaming receive mode](#streaming-receive-mode-optional): a receive buffer smaller than a single CAN FD frame is supported, as the remainder of a frame crossing a chunk boundary is carried over to the next chunk.

##### Signalling CAN FD frames to the CAN driver

Frames of more than 8 bytes can only be CAN FD frames, so a driver may derive the frame format from the length it is given. If it needs to be told explicitly, enable `-Disotpc_ENABLE_CAN_SEND_FLAGS=ON` (`ISO_TP_USER_SEND_CAN_FLAGS`) to add a flags argument to the shim, following the same pattern as the additional CAN argument described below:

```c
int isotp_user_send_can(const uint32_t arbitration_id, const uint8_t* data, const uint8_t size, const uint8_t flags) {
    if (flags & ISOTP_CAN_FRAME_FLAG_FD) {
        return CAN_SEND_FD(arbitration_id, data, size, (flags & ISOTP_CAN_FRAME_FLAG_BRS) != 0)
                   ? ISOTP_RET_ERROR
                   : ISOTP_RET_OK;
    }

    return CAN_SEND(arbitration_id, data, size) ? ISOTP_RET_ERROR : ISOTP_RET_OK;
}
```

Every frame of a link whose `TX_DL` exceeds 8 bytes carries `ISOTP_CAN_FRAME_FLAG_FD`, including short single frames and flow control frames, so a link either speaks CAN FD or it doesn't. `ISOTP_CAN_FRAME_FLAG_BRS` is added on top of that if `-Disotpc_ENABLE_CAN_FD_BRS=ON` (`ISO_TP_CAN_FD_USE_BRS`) is set, asking the driver to transmit the data phase at the higher CAN FD bit rate.

If both this option and `ISO_TP_USER_SEND_CAN_ARG` are enabled, the flags precede the user argument: `isotp_user_send_can(id, data, size, flags, arg)`.

#### Use of multiple CAN interfaces
For applications requiring multiple CAN interfaces, it is necessary to specify the interface in `isotp_user_send_can`. 

In this case the config option `-DISO_TP_USER_SEND_CAN_ARG` may be enabled. The library may then be used as follows:

```c
// Objects representing two CAN interfaces: a and b.
CAN_IFACE_t can_a, can_b;

void init() {
    // Two IsoTpLinks assumed to be bound to different CAN interfaces.
    IsoTpLink link_a, link_b;

    isotp_init_link(&link_a, ...);
    isotp_init_link(&link_b, ...);

    // After link initialization, the relevant CAN interface may be
    // attached to the link. 
    link_a.user_send_can_arg = &can_a;
    link_a.user_send_can_arg = &can_b;
}

int isotp_user_send_can(
    const uint32_t arbitration_id, 
    const uint8_t *data, 
    const uint8_t size,
    void *user_send_can_arg) 
{
    // It is then available for use inside isotp_user_send_can
    int err = CAN_SEND((CAN_IFACE_t *)(user_send_can_arg), arbitration_id, data, size);
    if (err) {
        return ISOTP_RET_ERROR;
    } else {
        return ISOTP_RET_OK;
    }
}

```

#### Enable event-driven messaging

Version 1.6.0 features a new event-driven messaging model, which is **disabled by default**.  
In order to enable this feature, the following CMake option(s) must be passed:

```cmake
set(isotpc_ENABLE_TRANSCEIVE_EVENTS ON CACHE BOOL "Enable message events in isotp-c")

# Optionally enable/disable send/receive events:
# set(isotpc_ENABLE_TRANSMIT_COMPLETE_CALLBACK OFF CACHE BOOL "Optionally enables or disables sending/receiving events")
# set(isotpc_ENABLE_RECEIVE_COMPLETE_CALLBACK OFF CACHE BOOL "Optionally enables or disables sending/receiving events")
```

These options can also be passed via the command-line: `-Disotpc_ENABLE_TRANSCEIVE_EVENTS=ON`.

##### Enabling these options using Makefiles

If you're still using Makefiles (**NOT** recommended for this project!), then you will have to modify the `isotp_config.h` header file and enable the options manually.  
This is **NOT RECOMMENDED**, however, as this file will be overwritten by new versions of the library.

#### Inclusion in your CMake project
```cmake
###
# Set your desired options
###
set(isotpc_USE_INCLUDE_DIR ON CACHE BOOL "Use external include directory for isotp-c") # optional
set(isotpc_STATIC_LIBRARY ON CACHE BOOL "Build isotp-c as a static library instead of shared") # optional

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/path/to/isotp-c) # add to current project

target_link_libraries(
    mytarget

    # ... other libs
    simon_cahill::isotp_c
)
```


## Usage

First, create some [shim](https://en.wikipedia.org/wiki/Shim_(computing)) functions to let this library use your lower level system:

```C
    /* required, this must send a single CAN message with the given arbitration
     * ID (i.e. the CAN message ID) and data. The size will never be more than 8
     * bytes, or more than the link's TX_DL if CAN FD is enabled; sizes of more
     * than 8 bytes have to be sent as a CAN FD frame.
     * Should return ISOTP_RET_OK if frame sent successfully.
     * May return ISOTP_RET_NOSPACE if the frame could not be sent but may be
     * retried later. Should return ISOTP_RET_ERROR in case frame could not be sent.
     */
    int  isotp_user_send_can(const uint32_t arbitration_id,
                             const uint8_t* data, const uint8_t size) {
        // ...
    }

    /* required, return system tick, unit is micro-second */
    uint32_t isotp_user_get_us(void) {
        // ...
    }
    
    /* optional, provide to receive debugging log messages */
    void isotp_user_debug(const char* message, ...) {
        // ...
    }
```

### API

You can use isotp-c in the following way:

#### Traditional polling mode

```C
    /* Alloc IsoTpLink statically in RAM */
    static IsoTpLink g_link;

	/* Alloc send and receive buffer statically in RAM */
    static uint8_t g_isotpRecvBuf[ISOTP_BUFSIZE];
    static uint8_t g_isotpSendBuf[ISOTP_BUFSIZE];
	
    int main(void) {
        /* Initialize CAN and other peripherals */
        
        /* Initialize link, 0x7TT is the CAN ID you send with */
        isotp_init_link(&g_link, 0x7TT,
						g_isotpSendBuf, sizeof(g_isotpSendBuf), 
						g_isotpRecvBuf, sizeof(g_isotpRecvBuf));
        
        while(1) {
        
            /* If receive any interested can message, call isotp_on_can_message to handle message */
            ret = can_receive(&id, &data, &len);
            
            /* 0x7RR is CAN ID you want to receive */
            if (RET_OK == ret && 0x7RR == id) {
                isotp_on_can_message(&g_link, data, len);
            }
            
            /* Poll link to handle multiple frame transmition */
            isotp_poll(&g_link);
            
            /* You can receive message with isotp_receive.
               payload is upper layer message buffer, usually UDS;
               payload_size is payload buffer size;
               out_size is the actuall read size;
               */
            ret = isotp_receive(&g_link, payload, payload_size, &out_size);
            if (ISOTP_RET_OK == ret) {
                /* Handle received message */
            }
            
            /* And send message with isotp_send */
            ret = isotp_send(&g_link, payload, payload_size);
            if (ISOTP_RET_OK == ret) {
                /* Send ok */
            } else {
                /* An error occured */
            }
            
            /* In case you want to send data w/ functional addressing, use isotp_send_with_id */
            ret = isotp_send_with_id(&g_link, 0x7df, payload, payload_size);
            if (ISOTP_RET_OK == ret) {
                /* Send ok */
            } else {
                /* Error occur */
            }
        }

        return;
    }
```
    
You can call isotp_poll as frequently as you want, as it internally uses isotp_user_get_ms to measure timeout occurences.
If you need handle functional addressing, you must use two separate links, one for each.

#### Streaming receive mode (optional)

Enable `isotpc_ENABLE_STREAMING` to receive a message larger than the link's
receive buffer without allocating space for the complete message:

```cmake
set(isotpc_ENABLE_STREAMING ON CACHE BOOL "Enable chunked ISO-TP reception")
```

Call `isotp_receive_streaming()` in the same polling loop used for
`isotp_receive()`. Each successful call returns the next chunk. The library
uses flow control to pause the sender whenever the internal receive buffer is
full, and resumes reception after the application consumes that chunk.

```C
bool message_complete;
uint32_t chunk_size;

ret = isotp_receive_streaming(&g_link, chunk, sizeof(chunk),
                              &chunk_size, &message_complete);
if (ret == ISOTP_RET_OK) {
    write_chunk_to_flash(chunk, chunk_size);
    if (message_complete) {
        finish_update();
    }
}
```

When building without CMake, define `ISO_TP_ENABLE_STREAMING` in
`isotp_config.h`. The feature is disabled by default and does not change the
link structure or public API unless enabled. Oversized messages use the
streaming polling API even when receive callbacks are configured.

```C
    /* Alloc IsoTpLink statically in RAM */
    static IsoTpLink g_phylink;
    static IsoTpLink g_funclink;

	/* Allocate send and receive buffer statically in RAM */
	static uint8_t g_isotpPhyRecvBuf[512];
	static uint8_t g_isotpPhySendBuf[512];
	/* currently functional addressing is not supported with multi-frame messages */
	static uint8_t g_isotpFuncRecvBuf[8];
	static uint8_t g_isotpFuncSendBuf[8];	
	
    int main(void) {
        /* Initialize CAN and other peripherals */
        
        /* Initialize link, 0x7TT is the CAN ID you send with */
        isotp_init_link(&g_phylink, 0x7TT,
						g_isotpPhySendBuf, sizeof(g_isotpPhySendBuf), 
						g_isotpPhyRecvBuf, sizeof(g_isotpPhyRecvBuf));
        isotp_init_link(&g_funclink, 0x7TT,
						g_isotpFuncSendBuf, sizeof(g_isotpFuncSendBuf), 
						g_isotpFuncRecvBuf, sizeof(g_isotpFuncRecvBuf));
        
        while(1) {
        
            /* If any CAN messages are received, which are of interest, call isotp_on_can_message to handle the message */
            ret = can_receive(&id, &data, &len);
            
            /* 0x7RR is CAN ID you want to receive */
            if (RET_OK == ret) {
                if (0x7RR == id) {
                    isotp_on_can_message(&g_phylink, data, len);
                } else if (0x7df == id) {
                    isotp_on_can_message(&g_funclink, data, len);
                }
            } 
            
            /* Poll link to handle multiple frame transmition */
            isotp_poll(&g_phylink);
            isotp_poll(&g_funclink);
            
            /* You can receive message with isotp_receive.
               payload is upper layer message buffer, usually UDS;
               payload_size is payload buffer size;
               out_size is the actuall read size;
               */
            ret = isotp_receive(&g_phylink, payload, payload_size, &out_size);
            if (ISOTP_RET_OK == ret) {
                /* Handle physical addressing message */
            }
            
            ret = isotp_receive(&g_funclink, payload, payload_size, &out_size);
            if (ISOTP_RET_OK == ret) {
                /* Handle functional addressing message */
            }            
            
            /* And send message with isotp_send */
            ret = isotp_send(&g_phylink, payload, payload_size);
            if (ISOTP_RET_OK == ret) {
                /* Send ok */
            } else {
                /* An error occured */
            }
        }

        return;
    }
```


#### Event-driven mode (optional)

If you enabled callback support during build, you can use event-driven programming instead of polling:

```C
    /* Optional: Set up callbacks for event-driven programming */
    void on_message_sent(void* link, uint32_t size, void* user_arg) {
        printf("Message transmission complete: %u bytes\n", size);
        // Handle transmission complete event
    }
    
    void on_message_received(void* link, const uint8_t* data, uint32_t size, void* user_arg) {
        printf("Message received: %u bytes\n", size);
        // Process received data directly - no need to call isotp_receive()
        process_isotp_message(data, size);
    }
    
    int main(void) {
        /* Initialize CAN and other peripherals */
        
        /* Initialize link */
        isotp_init_link(&g_link, 0x7TT,
						g_isotpSendBuf, sizeof(g_isotpSendBuf), 
						g_isotpRecvBuf, sizeof(g_isotpRecvBuf));
        
        /* Set callbacks (optional - if callbacks not set, use traditional polling) */
        isotp_set_tx_done_cb(&g_link, on_message_sent, &g_link);
        isotp_set_rx_done_cb(&g_link, on_message_received, &g_link);
        
        while(1) {
            /* Handle incoming CAN messages */
            ret = can_receive(&id, &data, &len);
            if (RET_OK == ret && 0x7RR == id) {
                isotp_on_can_message(&g_link, data, len);
            }
            
            /* Poll link - callbacks will be called automatically when complete */
            isotp_poll(&g_link);
            
            /* Send message */
            ret = isotp_send(&g_link, payload, payload_size);
            if (ISOTP_RET_OK == ret) {
                /* Send initiated - on_message_sent will be called when complete */
            }
            
            /* Note: No need to poll isotp_receive() when using rx callback */
        }

        return;
    }
```

## Authors

Please view [Contributors](#contributors) to see a list of all contributors.

## License

Licensed under the MIT license.
