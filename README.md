# plotty

Simple to use serial plotting program. It uses [raylib](https://www.raylib.com/) to handle drawing.

## Quick start

Make sure you have installed raylib 5.0 on your system (on Arch Linux it can be done
by installing `raylib` package). Then run provided build script:

```console
./build.sh
```

and run the application:

```console
./plotty
```

You can check defaults in provided usage message:

```console
./plotty -h
```

## Example of microcontroller code

This example was created for Raspberry PI Pico using official C SDK:

```c
#include <hardware/adc.h>
#include <hardware/gpio.h>
#include <inttypes.h>
#include <pico/stdlib.h>
#include <pico/time.h>
#include <stdio.h>

int main() {
    stdio_init_all();

    adc_init();

    gpio_init(26);
    gpio_init(27);

    adc_set_round_robin(0b11);

    while (true) {
        const float factor = 3.3f / (1 << 12);
        uint16_t res1 = adc_read();
        uint16_t res2 = adc_read();

        absolute_time_t now = get_absolute_time();
        uint64_t now_usec = to_us_since_boot(now);
        printf("%" PRIu64 ":%f;%f\n", now_usec, res1 * factor, res2 * factor);
        sleep_ms(1);
    }
}
```
