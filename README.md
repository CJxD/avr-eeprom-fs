## A (mostly) wear-leveling EEPROM filesystem

*Warning*:
While the fundamentals behind this filesystem are sound, there is a known bug with appending to files that can result in data corruption. Therefore it's not recommended to use this in production code without rigorous testing (Please contribute with any improvements! I only worked on this for one week!)

### What's wear-leveling?
Non-volatile memory like EEPROM has a limited write lifespan (for example, 100,000 writes). If data changes regularly, this can exhaust blocks of the EEPROM until they're unusable.

Wear-leveling gets around this problem by writing to new areas of the memory every time a write is requested.

Why mostly? Well, while I tried to make the file-table be written to new blocks every time it's modified, just the same as the data, it ends up as a chicken-and-egg problem. Hence, the file-table is static. If you can work out an efficient way to get around this limitation, please contribute!

### Usage

See example.c
