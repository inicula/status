#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <charconv>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

using u8 = uint8_t;
using u64 = uint64_t;

#define SOCKET_PATH "\0/tmp/status.socket"

int
main(int argc, char* argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: status <index-of-update-commands>...\n");
        return EXIT_FAILURE;
    }

    u64 bs = 0;
    for (int i = 1; i < argc; i++) {
        u8 pos;
        if (std::from_chars(argv[i], argv[i] + strlen(argv[i]), pos).ec != std::errc{}) {
            fprintf(stderr, "status: std::from_chars failed\n");
            return EXIT_FAILURE;
        }

        if (pos >= 64) {
            fprintf(stderr, "status: Position %d is out of bounds\n", pos);
            return EXIT_FAILURE;
        }

        bs |= u64(1) << pos;
    }

    int server_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (sendto(server_fd, &bs, sizeof(bs), 0, (const sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("sendto");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
