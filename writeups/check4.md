Checkpoint 4 Writeup
====================

Linux TCP/IP

User construct IP packet, including IP header. Then push them to TUN devices. Then kernel write Ethernet header, and send the packet via phisical Ethernet card.

Host unreachable

In real life, an interface will eventually send an ICMP “host unreachable” back across the
Internet to the original sender if it can’t get a reply to its ARP requests.


#### span 
span 当作一个连续对象的 view，但不 own 对象。内部实际只存储了一个指针和一个长度。span 对于静态和动态存储有略不同的执行模式
- `M_ptr` 保存连续对象的起始地址
- `[[no_unique_address]] extent_storage _M_extent` 是 extent 的 storage，在 static 情况下，sizeof(extent_storage) = 0. 长度存储在泛型参数中。dynamic 时，在内存中保存创建时的长度。

操他妈的，引用和指针的安全性基本上也没啥区别。 Timer 里面定义了一个引用，复制构造函数，移动构造函数忘了控制了，直接炸了！！！！！！！！！！