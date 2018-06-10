# memorylog
Logging into memory buffer, as fast as possible (almost). It is very fast and multithreaded (wait-free algorithm to deal with shared structures).
The project moved to gitlab: https://gitlab.com/agrianius/memorylog .

## Motivation
Motivation for the library begins with a bug which was very hard to catch. I had a distributed system with about several hundreds computers. The computers was executing a program which had a state machine. Sometimes (about ones in an hour of cluster activity) internal invariants of the state machine became broken. I could not figure out how this could happened from the source code of the program, so I needed a log of all transitions in the state machine. But the problem was - it was A LOT OF DATA. Logging to a file made the program 100 times slower and I did not really understand what to do with terabytes of logs. So I decided to log transitions into a memory buffer with several gigabytes. When state machine invariants was broken the program aborted and I could read the log from the coredump of the program. That gave me the last seconds of program life time but that was enough to catch the bug and fix it

## How to use it and basic properties
Include memorylog.hh, use cmake and build memorylog target to make a static library.

Call "initialize(total_buffer_size, chunk_size)" at the start of a program, returns true if successful. "initialize" allocates a buffer of size "total_buffer_size", divide the buffer into chunks of size "chunk_size" ("total_buffer_size" must be a multiple of "chunk_size") and put the chunks into internal lock-free ring queue (it's wait-free for most cases and lock-free under heavy load which is unlikely to occure).

At the end of the program you may call "finalize" to make your memory-leak detection silent but it is not really necessary in most cases.

To log something call "write" or "format_write", returns true if successful. The functions get a chunk from the queue and put a record into the chunk. The chunk is saved into a TLS variable for future use, so subsequent calls of the functions will use the saved chunk. When the chunk is full a thread returns the full chunk into the queue and get another chunk from the queue. The queue has no order guarantee but it has high probability to be effectively ordered. See manual for "printf" from libc to know how to work with "format_write".

Call "dump" to write the whole buffer into a file, returns true if successfull. It is also possible to find log in a coredump of a program.

Each record has a prefix "\\niPao2ijSahbe0F ", thus greping "^iPao2ijSahbe0F .\*" from a coredump allows you to find completely written records and you can avoid partial records or garbage.
