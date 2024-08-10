#include <fcntl.h>
#include <inttypes.h>
#include <raylib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

int setup_port(const char* portname) {
    int fd = open(portname, O_RDONLY | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        return -1;
    }

    cfsetspeed(&tty, B115200);
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;  // 8-bit chars
    tty.c_iflag &= IGNBRK;                       // disable break processing
    tty.c_lflag = 0;                             // no signalling chars
    tty.c_oflag = 0;                             // no remapping

    tty.c_cc[VMIN] |= 0;   // read does not block
    tty.c_cc[VTIME] |= 0;  // 0.0s read timeout

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);  // shut off xon/xoff ctrl
    tty.c_cflag |= (CLOCAL | CREAD);    // ignore model controls, enable reading
    tty.c_cflag &= ~(PARENB | PARODD);  // shut off parity
    tty.c_cflag &= ~(CSTOP);
    tty.c_cflag &= ~(CRTSCTS);

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        return -1;
    }

    return fd;
}

void parse_channel_data(char* data, uint64_t* timestamp, float* output,
                        int* output_count, int max_channels) {
    char* cursor = data;
    *output_count = 0;
    {
        char* next;
        *timestamp = strtoll(cursor, &next, 10);
        if (next == cursor || *next != ':') return;
        cursor = next + 1;
    }
    for (int i = 0; i < max_channels; i++) {
        char* next;
        output[i] = strtof(cursor, &next);
        if (next == cursor) {
            return;
        }
        *output_count += 1;
        if (*next == ';') {
            cursor = next + 1;
            continue;
        }
        return;
    }
}

void usage(FILE* f, char* program) {
    fprintf(f, "Usage: %s <OPTIONS>\n", program);
    fprintf(f, "OPTIONS:\n");
    fprintf(f, "  -p portname    tty port to read data from [/dev/ttyACM0]\n");
    fprintf(f, "  -p count       max channels expected in stream [2]\n");
    fprintf(f, "  -s count       max samples stored in buffer [1024]\n");
    fprintf(f, "  -S count       max samples stored to read in one frame [10]\n");
    fprintf(f, "  -u float       lower bound of expected values [3.3]\n");
    fprintf(f, "  -l float       upper bound of expected values [0.0]\n");
    fprintf(f, "  -T usecs       time period to display in view [1000000]\n");
    fprintf(f, "  -h             show this info on stdout\n");
    fprintf(f, "INPUT FORMAT:\n");
    fprintf(f, "  Each sample is descibed as a line of format:\n");
    fprintf(f, "    TIME:CHAN1;CHAN2...\n");
}

int main(int argc, char** argv) {
    printf("Hello, Plotty!\n");

    char* portname = "/dev/ttyACM0";
    int max_channels = 2;
    int max_sample_count = 1024;
    int max_samples_per_frame = 10;
    float value_upper = 3.3f;
    float value_lower = 0.0f;
    uint64_t display_period = 1000 * 1000;

    // PARAMS

    int opt;
    while ((opt = getopt(argc, argv, "p:c:s:S:u:l:T:h")) != -1) {
        switch (opt) {
            case 'p':
                portname = optarg;
                break;
            case 'c':
                max_channels = atoi(optarg);
                break;
            case 's':
                max_sample_count = atoi(optarg);
                break;
            case 'S':
                max_samples_per_frame = atoi(optarg);
                break;
            case 'u':
                value_upper = atof(optarg);
                break;
            case 'l':
                value_lower = atof(optarg);
                break;
            case 'T':
                display_period = atoll(optarg);
                break;
            case 'h':
                usage(stdout, argv[0]);
                return EXIT_SUCCESS;
            default:
                usage(stderr, argv[0]);
                return EXIT_FAILURE;
        }
    }

    // INIT

    int fd = setup_port(portname);
    if (fd < 0) {
        return EXIT_FAILURE;
    }
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1000, 800, TextFormat("plotty: %s", portname));
    SetTargetFPS(60);

    // LOOP

    int view_width = 600;
    int view_height = 400;
    int view_offset_x = 10;
    int view_offset_y = 50;

    bool paused = 0;
    float* sample_value_buffer =
        malloc(sizeof(float) * max_sample_count * max_channels);
    uint64_t* sample_timestamp_buffer =
        malloc(sizeof(uint64_t) * max_sample_count);
    Color colors[2] = {RED, GREEN};
    int sample_cursor = 0;

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_SPACE)) {
            paused = !paused;
        }

        for (int i = 0; i < max_samples_per_frame; i++) {
            float channels[max_channels];
            int channel_count = 0;
            uint64_t timestamp;

            char buf[1024];
            int bytes_read = read(fd, buf, sizeof(buf) - 1);
            if (bytes_read == 0) break;
            buf[bytes_read] = '\0';

            if (!paused) {
                parse_channel_data(buf, &timestamp, channels, &channel_count,
                                   max_channels);
                if (timestamp == 0) {
                    break;
                }
                for (int c = 0; c < channel_count; c++) {
                    sample_value_buffer[c * max_sample_count + sample_cursor] =
                        channels[c];
                }
                sample_timestamp_buffer[sample_cursor] = timestamp;
                sample_cursor += 1;
                sample_cursor %= max_sample_count;
            }
        }

        BeginDrawing();
        {
            ClearBackground(BLACK);

            view_width = GetScreenWidth() - 20;
            view_height = GetScreenHeight() - 50 - max_channels * 25;

            uint64_t now =
                sample_timestamp_buffer[(sample_cursor + max_sample_count - 1) %
                                        max_sample_count];

            DrawText(TextFormat("display period: %.3fs, value: <%.3f, %.3f>, "
                                "port: %s, board time: %.03fs",
                                display_period / 1e6f, value_lower, value_upper,
                                portname, now / 1e6f),
                     10, 10, 20, WHITE);

            DrawRectangleLines(view_offset_x, view_offset_y, view_width,
                               view_height, WHITE);

            float highligted_channel_values[max_channels];
            int mouse_x = GetMouseX();
            if (mouse_x < view_offset_x) mouse_x = view_offset_x;
            if (mouse_x > view_offset_x + view_width)
                mouse_x = view_offset_x + view_width;

            int mouse_y = GetMouseY();
            if (mouse_y < view_offset_y) mouse_y = view_offset_y;
            if (mouse_y > view_offset_y + view_height)
                mouse_y = view_offset_y + view_height;

            DrawLine(view_offset_x, mouse_y, view_offset_x + view_width,
                     mouse_y, GRAY);
            DrawLine(mouse_x, view_offset_y, mouse_x,
                     view_offset_y + view_height, GRAY);

            for (int c = 0; c < max_channels; c++) {
                float ly = 0;
                float lx = 0;
                int pc = 0;
                highligted_channel_values[c] = 0;
                for (int s = sample_cursor;
                     s < sample_cursor + max_sample_count; s++) {
                    int si = s % max_sample_count;
                    if (now - sample_timestamp_buffer[si] > display_period)
                        continue;

                    uint64_t t = sample_timestamp_buffer[si];
                    float v = sample_value_buffer[c * max_sample_count + si];

                    int x =
                        view_width - (now - t) * view_width / display_period;
                    int y = (1.0f -
                             (v - value_lower) / (value_upper - value_lower)) *
                            view_height;

                    if (lx < mouse_x - view_offset_x &&
                        (x >= mouse_x - view_offset_x ||
                         sample_timestamp_buffer[si] == now)) {
                        DrawCircle(x + view_offset_x, y + view_offset_y, 3,
                                   colors[c]);
                        highligted_channel_values[c] = v;
                    }

                    if (pc > 0) {
                        DrawLine(view_offset_x + lx, view_offset_y + ly,
                                 view_offset_x + x, view_offset_y + y,
                                 colors[c]);
                    }
                    ly = y;
                    lx = x;
                    pc++;
                }
            }

            for (int c = 0; c < max_channels; c++) {
                DrawText(TextFormat("Channel %d: %.3f", c,
                                    highligted_channel_values[c]),
                         view_offset_x,
                         view_offset_y + view_height + 5 + 25 * c, 20,
                         colors[c]);
            }
            DrawText(TextFormat("Cursor t: %.3fs",
                                ((float)(mouse_x - view_offset_x) / view_width -
                                 1.0f) *
                                    display_period / 1e6f),
                     view_offset_x + view_width / 2,
                     view_offset_y + view_height + 5, 20, WHITE);
            DrawText(TextFormat(
                         "Cursor v: %.3f",
                         (1.0 - (float)(mouse_y - view_offset_y) / view_width) *
                                 (value_upper - value_lower) +
                             value_lower),
                     view_offset_x + view_width / 2,
                     view_offset_y + view_height + 5 + 25, 20, WHITE);
        }
        EndDrawing();
    }

    // CLEANUP

    close(fd);
    free(sample_timestamp_buffer);
    free(sample_value_buffer);

    return EXIT_SUCCESS;
}
