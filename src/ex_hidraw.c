/*
 *  Copyright 2016 Justin Schneck
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <poll.h>
#include <err.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <linux/types.h>

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "utils.h"
#include "erlcmd.h"
#include "hidraw_enum.h"
#include "ex_hidraw.h"

void device_handle_output_request(const char *buf, void *cookie) {
    int index, version, fd;
    char message[MAX_REPORT_SIZE];
    long len;

    debug("index %d", index);
    index = sizeof(uint16_t);
    debug("%d = sizeof(uint16_t);", index);
    index++; // Skip id
    debug("%d++; // Skip id", index);
    ei_decode_version(buf, &index, &version);
    debug("ei_decode_version(buf, &%d, &%d);", index, version);
    ei_decode_binary(buf, &index, message, &len);
    debug("ei_decode_binary(buf, &%d, %p, &%d);", index, message, len);

    fd = *(int *) cookie;

    size_t wrote = 0;
    do {
        ssize_t amount_written = write(fd, message + wrote, len - wrote);
        debug("ssize_t amount_written = write(fd, message + wrote, len - wrote);");
        debug("ssize_t %d = write(%d, %p + %d, %d - %d);", amount_written, fd, message, wrote, len, wrote);
        if (amount_written < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            err(EXIT_FAILURE, "write");
        }
        wrote += amount_written;
    } while (wrote < len);
}

void device_handle_descriptor_request(const char *buf, void *cookie) {
    int fd;
    int res, desc_size, resp_index;
    char resp[MAX_RESPONSE_SIZE];

    struct hidraw_report_descriptor rpt_desc;
    // struct hidraw_devinfo info;

    memset(&rpt_desc, 0x0, sizeof(rpt_desc));
    // memset(&info, 0x0, sizeof(info));

    fd = *(int *) cookie;

    /* Get Report Descriptor Size */
    res = ioctl(fd, HIDIOCGRDESCSIZE, &desc_size);
    if (res < 0)
        perror("HIDIOCGRDESCSIZE");
    else
            debug("Report Descriptor Size: %d\n", desc_size);

    /* Get Report Descriptor */
    rpt_desc.size = desc_size;
    res = ioctl(fd, HIDIOCGRDESC, &rpt_desc);
    if (res < 0)
        perror("HIDIOCGRDESC");
    else
            debug("%s ", rpt_desc.value);

    resp_index = sizeof(uint16_t); // Space for payload size
    resp[resp_index++] = DESCRIPTOR_REPORT_ID;
    ei_encode_version(resp, &resp_index);
    ei_encode_binary(resp, &resp_index, rpt_desc.value, desc_size);
    erlcmd_send(resp, resp_index);
}

void device_handle_request(const char *buf, void *cookie) {
    int index;
    char id;

    index = sizeof(uint16_t);
    id = buf[index];

    switch (id) {
        case DESCRIPTOR_REPORT_ID:
            device_handle_descriptor_request(buf, cookie);
            break;
        case OUTPUT_ID:
            device_handle_output_request(buf, cookie);
            break;
    }
}

void device_process(int fd) {
    int i, res, resp_index;
    char buf[MAX_REPORT_SIZE], resp[MAX_RESPONSE_SIZE];

    /* Get a report from the device */
    res = read(fd, buf, MAX_REPORT_SIZE);
    /* This call should never fail as the function is only called if data is ready to be read (poll). */
    if (res < 0)
        err(EXIT_FAILURE, "read from HID Device");

    if (res ==0)
        return;

    debug("read() read %d bytes:\n\t", res);

    resp_index = sizeof(uint16_t); // Space for payload size
    resp[resp_index++] = INPUT_REPORT_ID;
    ei_encode_version(resp, &resp_index);
    ei_encode_binary(resp, &resp_index, buf, res);
    erlcmd_send(resp, resp_index);
}

void device_closed(int fd) {
    debug("device closed");
    char resp[MAX_RESPONSE_SIZE];
    int resp_index = sizeof(uint16_t); // Space for payload size

    resp[resp_index++] = ERROR_ID;
    ei_encode_version(resp, &resp_index);
    ei_encode_tuple_header(resp, &resp_index, 2);
    ei_encode_atom(resp, &resp_index, "error");
    ei_encode_atom(resp, &resp_index, "closed");
    erlcmd_send(resp, resp_index);
}

static int open_device(char *dev) {
    int fd;
    struct erlcmd handler;

    debug("Open device %s", dev);
    fd = open(dev, O_RDWR | O_NONBLOCK);
    if (errno == EACCES && getuid() != 0)
        err(EXIT_FAILURE, "You do not have access to %s.", dev);

    erlcmd_init(&handler, device_handle_request, &fd);

    for (;;) {
        struct pollfd fdset[2];

        fdset[0].fd = STDIN_FILENO;
        fdset[0].events = POLLIN;
        fdset[0].revents = 0;

        fdset[1].fd = fd;
        fdset[1].events = (POLLIN | POLLPRI | POLLHUP);
        fdset[1].revents = 0;

        int timeout = -1; // Wait forever unless told by otherwise
        int rc = poll(fdset, 2, timeout);

        if (fdset[0].revents & (POLLIN | POLLHUP)) // Command or quit from Erlang
            erlcmd_process(&handler);

        if (fdset[1].revents & POLLIN) // New report on hidraw device
            device_process(fd);

        if (fdset[1].revents & POLLHUP) { // hidraw device closed
            device_closed(fd);
            break;
        }
    }
    debug("Exit");
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc == 2 && strcmp(argv[1], "enumerate") == 0)
        return enum_devices();
    else
        return open_device(strdup(argv[argc - 1]));
}
