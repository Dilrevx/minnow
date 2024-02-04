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


### 特点
- 报文的类型不拘泥于标准，可以先用易读的语言写好，发送时转换
- 面向对象，泛型编程，框架有一些设计模式。例如测试框架，要做的步骤可以固定拆解，于是有比较好的框架。
- 对 clib 进行了 OOP 封装，使用 .data() 等方式访问连续数组

## CMake 结构
- app, util, test, src 各自子目录通过指定 .cc 源文件的方法定义 library target，executable target 和 custom target。对于 custom target，在子目录 scope 下定义 custom command 和 dependency。
- 全局 scope 定义了编译器变量、include 目录、编译类型等参数，导入子目录

### Lab6 

Lab6 在 TA 服务器做了双向的 **UDP** 端口转发，sender <-> 90 <-> 91 <-> receiver. 

Lab6 框架实现了类似 linux kernel 的 api，叫做 `MinnowSocket<NI>`。该 socket 使用两个 thread 分别处理
1. read/write call <--> tcp/ip stack <--> adapter
2. adapter <--> router <--> UDP Socket(转发)

以 write 为例，app 使用 `MinnowSocket` 时，在前台 owner thread 调用 sock.write, 并在前台通过继承拼好了协议栈的连接方式。write 后，后台 TCPPeer thread 在 while(1) 中调用 Eventloop 的 `wait_next_event`，循环检测 incoming 数据，按照配置的 rule 将数据从一个接口传递给另一个接口。

#### Eventloop
Eventloop 是数据传递的核心，thread 调用 eventloop 的函数会不断让 Eventloop 等待下一个 round，一旦有 socket 准备好，在 round 中 eventloop 就会调用相应的 callback 函数，搬运数据。（占用当前 thread）

在实现上，Eventloop 需要先配置 rule，然后由 wait_next_event 轮询可用的 rule。

rule 分为 `BasicRule` 和 `FDRule`，
```c++
   struct BasicRule
  {
    size_t category_id;
    InterestT interest;
    CallbackT callback;
    bool cancel_requested {};
  };
  struct FDRule : public BasicRule
  {
    FileDescriptor fd;   
    Direction direction; 
    CallbackT cancel;    
    InterestT recover;   
```

rule 中，interest 函数判断 isvalid，callback 函数在触发时回调。

每次 wait_next_event 时，Eventloop 会在首个 valid 的 rule 处调用 callback 然后 return。
1. 首先，它检查 BasicRule，这些 rule 不需要 poll，interest 就调用
2. 然后检查 FDRule。如果 poll 退出并且 revent 没有错误，则调用相应的 callback 函数。

> `add_rule` 使用了一个巧妙的 forward 技巧，支持直接通过 str, args... 的方式构造 rule。模板函数的 `add_rule` 会对第一个 str 参数调用 `int add_category(string name)` 函数，注册 name 并将 str 转化为一个 int 的 id，这样参数就变成了 int, args...，进而可以匹配以 int 开头的重载函数。

Eventloop 相当于一个核心的 engine，外面挂载 router-host, router-Internet 的连接或是 Adapter-host 的连接

值得注意的是 POLLIN 可以不用配置在 POLLOUT 之前，因为不一定 interest

#### Adapter
Adapters 封装了 MinnowSocket 的底层通信 socket，该 socket 可能是 TUN/TAP device 也可能是 NetworkInterface & sockpair

#### MinnowSocket
这个类继承自 FDWrapper -> FD -> Socket -> LocalStreamSocket 序列，但把初始化的 fd 改为了 socket_pair.first，这样，fd 的行为会被重载改变，因为读和写不同步了

Socket 内通过 eventloop 配置了 read/write <--> stack <--> adapter 的连接，这样，无需重写 socket 的 read/write 函数

#### TCPSocketEndtoEnd
模拟的 linux socket api 需要一个实际进行底层通信的 fd。内部实现底层使用了一个 `AdaptT = NI` 类型的变量保存实际通信 sock。



TUN/TAP (IP/Ethernet tunneling)(https://www.kernel.org/doc/Documentation/networking/tuntap.txt)
- 用 ioctl 打开字符设备文件 (/dev/tun)，并指定一个 str 名字 tunX。
- tunX 代表了一个队列，sender/receiver 使用相同的名称，将数据包写入或从该设备队列读取。
    
        
user packet --> tcp/ip stack --> tun --> VPN --> tcp/ip stack --> Phyical NIC
- user 进行正常 socket 通信



实际运行时有三个 thread，除主线程外，
1. 第一个 eventloop 运行 tcp_main.tcp_loop, 
2. 第二个 eventloop 运行 endtoend 的 eventloop

*（原来 c++20 不是说 string 可以 constexpr，而是可以在编译时 *内存没有 cost* 的情况下支持计算*