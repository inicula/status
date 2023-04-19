#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cerrno>
#include <string_view>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <sys/fcntl.h>
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
    FI_VOL,
    FI_MIC,
    FI_LOAD,
    FI_MEM,
    FI_TEMP,
    FI_GOV,
    FI_DATE,
    N_FIELDS,
};

/* Macros */
#define SOCK_NAME "status-sock"
#define STATUS_FMT "[%s |%s |%s |%s |%s |%s |%s |%s]"
#define FIELD_BUF_MAX_SIZE (31 + 1)
#define STATUS_BUF_MAX_SIZE (FIELD_BUF_MAX_SIZE * N_FIELDS + sizeof(STATUS_FMT) + 1)
#define SHELL "/bin/sh"
#define TIME_CMD R"(date +%T)"
#define LOAD_CMD R"(uptime | awk '{print $(NF-2)}' | sed 's/,//g')"
#define TEMP_CMD R"(sensors | grep -F "Core 0" | awk '{print $3}' | sed 's/+//')"
#define VOL_CMD R"(amixer sget Master | tail -n1 | awk -F '\\[|\\]' '{print $2}')"
#define CHECK_MUTED_VOL R"(amixer sget Master | tail -n1 | awk -F '\\[|\\]' '{print $4}')"
#define MIC_CMD R"(amixer sget Capture | tail -n1 | awk -F '\\[|\\]' '{print $4}')"
#define MEM_CMD R"(free -h | awk '/^Mem:/ {print $3"/"$2}')"
#define DATE_CMD R"(date "+%a %d.%m.%Y")"
#define SHCMD(cmd)                                                                            \
    {                                                                                         \
        SHELL, "-c", cmd, nullptr                                                             \
    }

/* Globals */
static char field_buffers[N_FIELDS][FIELD_BUF_MAX_SIZE];
#ifndef NO_X11
static Display* display;
static int screen;
static Window root;
#endif

/* Function declarations */
static void refresh_status();
static int get_named_socket();
static ssize_t read_all(int, char*, size_t);
static bool read_cmd_output(const char*, char*, size_t);
static void update_time();
static void update_load();
static void update_temp();
static void update_volume();
static void update_mic();
static void update_mem();
static void update_gov();
static void update_date();
static void init_status();
#ifndef NO_X11
static bool init_x();
#endif

/* Updates */
static constexpr void (*updates[])() = {
    &update_time,   /* 0 */
    &update_load,   /* 1 */
    &update_temp,   /* 2 */
    &update_volume, /* 3 */
    &update_mic,    /* 4 */
    &update_mem,    /* 5 */
    &update_gov,    /* 6 */
    &update_date,   /* 7 */
};

/* Function definitions  */
void
refresh_status()
{
    char buf[STATUS_BUF_MAX_SIZE] = {};
    snprintf(buf,
             sizeof(buf),
             STATUS_FMT,
             field_buffers[0],
             field_buffers[1],
             field_buffers[2],
             field_buffers[3],
             field_buffers[4],
             field_buffers[5],
             field_buffers[6],
             field_buffers[7]);

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
    socklen_t path_size = std::min(sizeof(name.sun_path) - 2, strlen(SOCK_NAME));
    memcpy(name.sun_path + 1, SOCK_NAME, path_size);

    if (bind(sock_fd, (const sockaddr*)&name, sizeof(sa_family_t) + path_size + 1) < 0) {
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
read_cmd_output(const char* cmd, char* buf, size_t size)
{
    buf[0] = '\0'; /* Ignore previous contents */

    if (size < 2)
        return false;

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

        auto nbytes = read_all(pipe_fds[0], buf, size - 1);
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
update_time()
{
    read_cmd_output(TIME_CMD, field_buffers[FI_TIME], FIELD_BUF_MAX_SIZE);
}

void
update_load()
{
    read_cmd_output(LOAD_CMD, field_buffers[FI_LOAD], FIELD_BUF_MAX_SIZE);
}

void
update_temp()
{
    read_cmd_output(TEMP_CMD, field_buffers[FI_TEMP], FIELD_BUF_MAX_SIZE);
}

void
update_volume()
{
    if (!read_cmd_output(VOL_CMD, field_buffers[FI_VOL], FIELD_BUF_MAX_SIZE - 1))
        return;

    char muted_buf[4] = {};
    if (read_cmd_output(CHECK_MUTED_VOL, muted_buf, sizeof(muted_buf)) &&
        strcmp(muted_buf, "off") == 0) {
        strcat(field_buffers[FI_VOL], "*");
    }
}

void
update_mic()
{
    read_cmd_output(MIC_CMD, field_buffers[FI_MIC], FIELD_BUF_MAX_SIZE);
}

void
update_mem()
{
    read_cmd_output(MEM_CMD, field_buffers[FI_MEM], FIELD_BUF_MAX_SIZE);
}

void
update_gov()
{
    static int fd = -1;

    if (fd < 0)
        fd = open("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return;
    }

    char gov_buf[32] = {};
    lseek(fd, 0, SEEK_SET);
    auto nbytes = read(fd, gov_buf, sizeof(gov_buf) - 1);
    if (nbytes < 0) {
        perror("read");
        return;
    }

    if (nbytes == 0) {
        fprintf(stderr, "statusd: Governor file unexpectedly closed\n");
        close(fd);
        fd = -1;
        return;
    }

    gov_buf[nbytes] = '\0';
    if (nbytes > 0 && gov_buf[nbytes - 1] == '\n')
        gov_buf[--nbytes] = '\0';

    char* field_buf = field_buffers[FI_GOV];
    if (strcmp(gov_buf, "performance") == 0)
        strcpy(field_buf, "p");
    else if (strcmp(gov_buf, "ondemand") == 0)
        strcpy(field_buf, "d");
    else if (strcmp(gov_buf, "powersave") == 0)
        strcpy(field_buf, "s");
    else
        strcpy(field_buf, "unk");
}

void
update_date()
{
    read_cmd_output(DATE_CMD, field_buffers[FI_DATE], FIELD_BUF_MAX_SIZE);
}

void
init_status()
{
    for (auto& update : updates)
        update();
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
    if (sock_fd < 0)
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

#ifdef STATUS_DBG
        fprintf(stderr, "statusd: Received bitset %lu\n", bitset);
#endif

        for (u8 i = 0; i < std::size(updates); i++) {
            if (bitset & (u64(1) << i))
                updates[i]();
            bitset &= ~(u64(1) << i);
        }

        if (bitset)
            fprintf(stderr, "statusd: Ignoring out of bounds bit positions\n");

        refresh_status();
    }

    return EXIT_SUCCESS;
}
