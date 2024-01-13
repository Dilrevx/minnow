Checkpoint 4 Writeup
====================

Linux TCP/IP

User construct IP packet, including IP header. Then push them to TUN devices. Then kernel write Ethernet header, and send the packet via phisical Ethernet card.

Host unreachable

In real life, an interface will eventually send an ICMP “host unreachable” back across the
Internet to the original sender if it can’t get a reply to its ARP requests.