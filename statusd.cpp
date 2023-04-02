#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cerrno>
#include <string_view>
#include <tuple>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include "defer.h"
#ifndef NO_X11
#include <X11/Xlib.h>
#endif

/* Typedefs */
using u8 = uint8_t;
using u64 = uint64_t;

/* Enums */
enum FieldIndex : u8 {
    FI_TIME = 0,
    FI_LOAD,
    FI_TEMP,
    N_FIELDS,
};

enum UpdateType : u8 {
    UT_SHELL = 0, /* Update a field with a shell command's output */
    UT_INTERNAL,  /* Update a field using a function */
};

/* Macros */
#define SOCKET_PATH "\0/tmp/status.socket"
#define STATUS_FMT "[%s |%s |%s]"
#define FIELD_BUF_MAX_SIZE (31 + 1)
#define STATUS_BUF_MAX_SIZE (FIELD_BUF_MAX_SIZE * N_FIELDS + sizeof(STATUS_FMT) + 1)
#define SHELL "/bin/sh"
#define TIME_CMD R"(date +%H:%M:%S)"
#define LOAD_CMD R"(uptime | grep -wo "average: .*," | cut --delimiter=' ' -f2 | head -c4)"
#define TEMP_CMD R"(sensors | grep -F "Core 0" | awk '{print $3}' | cut -c2-5)"
#define SHCMD(cmd)                                                                            \
    {                                                                                         \
        SHELL, "-c", cmd, nullptr                                                             \
    }

/* Structs */
struct Update {
    UpdateType type;
    FieldIndex buffer_idx;
    union {
        const char* sh_cmd;
        void (*fptr)(char*);
    } args;
};

/* Globals */
static char field_buffers[N_FIELDS][FIELD_BUF_MAX_SIZE];
#ifndef NO_X11
static Display* display;
static int screen;
static Window root;
#endif

/* Functions declarations */
static void refresh_status();
static int get_named_socket();
static ssize_t read_all(int, char*, size_t);
static bool read_cmd_output(const char*, char*);
static void run_update(const Update*);
static void init_status();
#ifndef NO_X11
static bool init_x();
#endif

/* Update configs */
static constexpr Update updates[] = {
    /* Update the time field */
    {UT_SHELL, FI_TIME, {.sh_cmd = TIME_CMD}},
    /* Update the load field */
    {UT_SHELL, FI_LOAD, {.sh_cmd = LOAD_CMD}},
    /* Update the cpu temp field */
    {UT_SHELL, FI_TEMP, {.sh_cmd = TEMP_CMD}},
};

/* Functions definitions  */
void
refresh_status()
{
    char buf[STATUS_BUF_MAX_SIZE] = {};
    snprintf(&buf[0],
             sizeof(buf),
             STATUS_FMT,
             field_buffers[0],
             field_buffers[1],
             field_buffers[2]);

#ifdef NO_X11
    puts(buf);
#else
    XStoreName(display, root, buf);
    XFlush(display);
#endif
}

int
get_named_socket()
{
    int sock_fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sock_fd < 0) {
        perror("socket");
        return -1;
    }

    sockaddr_un name{};
    name.sun_family = AF_UNIX;
    strncpy(name.sun_path, SOCKET_PATH, sizeof(name.sun_path) - 1);

    if (bind(sock_fd, (const sockaddr*)&name, sizeof(name)) < 0) {
        perror("bind");
        return -1;
    }

    return sock_fd;
}

ssize_t
read_all(int fd, char* buf, size_t count)
{
    size_t read_so_far = 0;
    while (read_so_far < count) {
        auto nbytes = read(fd, buf + read_so_far, count - read_so_far);
        if (nbytes < 0) {
            if (errno == EINTR)
                continue;
            else
                return nbytes;
        }

        if (nbytes == 0)
            break;

        read_so_far += size_t(nbytes);
    }

    return ssize_t(read_so_far);
}

bool
read_cmd_output(const char* cmd, char* buf)
{
    int pipe_fds[2];
    if (pipe(pipe_fds) < 0) {
        perror("pipe");
        return false;
    }
    defer { close(pipe_fds[0]); };

    pid_t child = fork();
    if (child < 0) {
        perror("fork");
        close(pipe_fds[1]);
        return false;
    }
    defer { waitpid(child, nullptr, 0); };

    if (child == 0) {
        close(pipe_fds[0]);

        if (dup2(pipe_fds[1], STDOUT_FILENO) < 0) {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
        close(pipe_fds[1]);

        const char* child_argv[] = SHCMD(cmd);
        execv(child_argv[0], (char**)child_argv);

        perror("execv");
        exit(EXIT_FAILURE);
    } else {
        close(pipe_fds[1]);

        buf[0] = '\0'; /* Ignore previous contents */
        auto nbytes = read_all(pipe_fds[0], &buf[0], FIELD_BUF_MAX_SIZE - 1);
        if (nbytes < 0) {
            perror("read");
            return false;
        }

        /* Null-terminate and delete trailing newline */
        buf[nbytes] = '\0';
        if (nbytes > 0 && buf[nbytes - 1] == '\n')
            buf[nbytes - 1] = '\0';

        return true;
    }
}

void
run_update(const Update* update)
{
    switch (update->type) {
    case UT_SHELL:
        read_cmd_output(update->args.sh_cmd, field_buffers[update->buffer_idx]);
        break;
    case UT_INTERNAL:
        update->args.fptr(field_buffers[update->buffer_idx]);
        break;
    }
}

void
init_status()
{
    for (auto& el : updates)
        run_update(&el);
    refresh_status();
}

#ifndef NO_X11
bool
init_x()
{
    display = XOpenDisplay(nullptr);
    if (!display) {
        fprintf(stderr, "statusd: XopenDisplay() failed\n");
        return false;
    }

    screen = DefaultScreen(display);
    root = RootWindow(display, screen);
    return true;
}
#endif

int
main()
{
    int sock_fd = get_named_socket();
    if (sock_fd < 1)
        return EXIT_FAILURE;

#ifndef NO_X11
    if (!init_x())
        return EXIT_FAILURE;
#endif

    init_status();

    for (;;) {
        u64 bitset;
        auto nbytes = read(sock_fd, &bitset, sizeof(bitset));
        if (nbytes < 0) {
            perror("read");
            return EXIT_FAILURE;
        }

        if (nbytes == 0) {
            fprintf(stderr, "statusd: Socket unexpectedly closed\n");
            return EXIT_FAILURE;
        }

        if (nbytes != sizeof(bitset)) {
            fprintf(stderr, "statusd: failed to read all bytes for bitset\n");
            continue;
        }

        for (u8 i = 0; i < std::size(updates); i++) {
            if (bitset & (1 << i))
                run_update(&updates[i]);
            bitset &= ~(1 << i);
        }

        if (bitset)
            fprintf(stderr, "statusd: Ignoring out of bounds bit positions\n");

        refresh_status();
    }

    return EXIT_SUCCESS;
}
