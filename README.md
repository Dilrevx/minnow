Stanford CS 144 Networking Lab
==============================

These labs are open to the public under the (friendly) request that to
preserve their value as a teaching tool, solutions not be posted
publicly by anybody.

Website: https://cs144.stanford.edu

To set up the build system: `cmake -S . -B build`

To compile: `cmake --build build`

To run tests: `cmake --build build --target test`

To run speed benchmarks: `cmake --build build --target speed`

To run clang-tidy (which suggests improvements): `cmake --build build --target tidy`

To format code: `cmake --build build --target format`

----

Lab0 - Lab3 operates only over the TCP level. They implement a parsed TCPMessage format, and test `send`/`receive` in such a level, without considering underlying datalink transmission.

Lab4+ binds the TCP sender/receiver with datalink calls.

---

ByteStream/Writer/Reader: a stream that accept no more than capacity, dropping overwhelmed data

Assembler: Buffer incoming packets(no more than current writer capacity), feed them to writer.

TCP Sender/Receiver: A light-weighted program that `support talking to` legal peers.

Network Interface:
- ARP: Address Resolution Protocol

---

特点：
- 报文的类型不拘泥于标准，可以先用易读的语言写好，发送时转换
- 面向对象，泛型编程，框架有一些设计模式。例如测试框架，要做的步骤可以固定拆解，于是有比较好的框架。
- 