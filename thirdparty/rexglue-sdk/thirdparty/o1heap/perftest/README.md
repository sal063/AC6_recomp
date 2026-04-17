# HITL performance test suite

Simple firmware for an RP2350 (Pico 2) that measures the CPU cycles required to allocate and free a fixed-size block from O1Heap.
Results are printed over UART0 (GPIO0 TX).

## Sample output from RP2350

O1Heap:

```
op      bytes        min       mean        max      count
alloc   total         91        122        126   20000000
free    total         52         82        116   20000000
```

Newlib malloc/free:

```
op      bytes        min       mean        max      count
alloc   total        100        136       2014   20000000
free    total         58         94        234   20000000
```

The happy case is comparable for both; the mean is about 10% better for O1Heap;
the worst-case is about 1500% better for O1Heap.

## Build

```sh
export PICO_SDK_PATH=/path/to/pico-sdk
cmake -S . -B build -DPICO_BOARD=pico2
cmake --build build
```

The output UF2 is `build/o1heap_perftest.uf2`.

## Run

- Put the Pico 2 into BOOTSEL mode and flash the UF2.
- Connect a UART adapter to GPIO0 (TX) and GND.
- Open a serial terminal at 115200 8N1, e.g.:

```sh
picocom -b 115200 /dev/ttyUSB0
```

If you have the Pico SDK installed, you can build, flash, and open the serial console in one step:

```sh
./run.sh --sdk ~/apps/pico-sdk
```
