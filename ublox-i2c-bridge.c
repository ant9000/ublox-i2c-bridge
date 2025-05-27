#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>

typedef enum {
    START,
    UBX_PREAMBLE,
    UBX_HEADER,
    UBX_PAYLOAD,
    NMEA_PAYLOAD,
    RTCM_PREAMBLE,
    RTCM_HEADER,
    RTCM_PAYLOAD,
} input_state_t;

int main(int argc, char **argv) {
    int fd;
    char *device;
    int address;
    uint8_t reg;
    size_t length, packet_length;
    input_state_t state;
    uint8_t input[6+65535+2], *b; // UBX allows for up to 65535 bytes of payload, plus 6 of header and 2 of checksum
    uint8_t buffer[255];

    if (argc < 3) {
        printf("usage: %s <dev> <addr>\n", argv[0]);
        return 1;
    }

    device = argv[1];
    address = atoi(argv[2]);
    if (address == 0) {
        if (sscanf(argv[2], "0x%02x", &address) != 1) {
            printf("Wrong I2C device address %s\n", argv[2]);
            return 1;
        }
    }

    fd = open(device, O_RDWR);
    if (fd < 0) {
        printf("Could not open the I2C device %\n", device);
        return 1;
    }

    if (ioctl(fd, I2C_SLAVE, address) < 0) {
        printf("Could not set I2C device address 0x%02x\n", address);
        close(fd);
        return 1;
    }

    reg = 0xff;
    if (write(fd, &reg, 1) < 0) {
        printf("Failed to write i2c register address\n");
        close(fd);
        return 1;
    }

    fcntl(fileno(stdin), F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    b = input;
    state = START;
    packet_length = 0;
    while (1) {
        while (read(fileno(stdin), b, 1) == 1) {
            switch (state) {
                case START:
                    if (*b == 0xb5) { // µ
                        state = UBX_PREAMBLE;
                    } else if (*b == '$') {
                        state = NMEA_PAYLOAD;
                    } else if (*b == 0xd3) {
                        state = RTCM_PREAMBLE;
                    }
                    break;
                case UBX_PREAMBLE:
                    if (*b == 'b') {
                        state = UBX_HEADER;
                    } else {
                        state = START;
                    }
                    break;
                case UBX_HEADER:
                    if (b - input + 1 == 6) {
                        packet_length = 6 + (input[5] << 8) + input[4] + 2;
                        state = UBX_PAYLOAD;
                    }
                    break;
                case UBX_PAYLOAD:
                    if (b - input + 1 == packet_length) {
                        write(fd, input, packet_length);
                        state = START;
                    }
                    break;
                case NMEA_PAYLOAD:
                    if (*b == '\n') {
                        packet_length = b - input + 1;
                        write(fd, input, packet_length);
                        state = START;
                    }
                    break;
                case RTCM_PREAMBLE:
                    if (*b & 0xfc == 0x00) {
                        state = RTCM_HEADER;
                    } else {
                        state = START;
                    }
                    break;
                case RTCM_HEADER:
                    if (b - input + 1 == 3) {
                        packet_length = 3 + (input[1] << 8) + input[2] + 3;
                        state = RTCM_PAYLOAD;
                    }
                    break;
                case RTCM_PAYLOAD:
                    if (b - input + 1 == packet_length) {
                        write(fd, input, packet_length);
                        state = START;
                    }
                    break;
                default:
                    break;
            }
            b++;
            if (b - input + 1 == sizeof(input)) {
                // packet would overflow - discard data
                state = START;
            }
            if (state == START) {
                b = input;
                packet_length = 0;
            }
        }

        length = read(fd, &buffer, sizeof(buffer));
        if (length == 0) {
            printf("Could not read from device %s, address 0x%02x\n", device, address);
            close(fd);
            return 1;
        }
        for (int i=0; i<length; i++) {
            if (buffer[i] != 0xff) {
                putchar(buffer[i]);
            }
        }
    }
    close(fd);
    return 0;
}
