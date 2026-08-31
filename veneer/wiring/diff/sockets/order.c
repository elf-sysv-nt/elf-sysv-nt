/* sockets slice: byte order and address text -- htons and kin as
   round-trips, inet_aton's accepted forms, inet_ntop/inet_pton over
   v4 and v6 including the canonical v6 compression. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>

int main(void)
{
    struct in_addr a;
    struct in6_addr a6;
    char buf[INET6_ADDRSTRLEN];

    printf("htons %d\n", ntohs(htons(0x1234)) == 0x1234);
    printf("htonl %d\n", ntohl(htonl(0x12345678u)) == 0x12345678u);
    printf("net16 %u\n", (unsigned)htons(1));
    printf("net32 %lu\n", (unsigned long)htonl(1));

    printf("aton %d\n", inet_aton("192.168.1.2", &a) == 1);
    printf("ntoa %s\n", inet_ntoa(a));
    printf("aton-class %d\n", inet_aton("127.1", &a) == 1);
    printf("ntoa-class %s\n", inet_ntoa(a));
    printf("aton-hex %d\n", inet_aton("0x7f.0.0.1", &a) == 1);
    printf("aton-bad %d\n", inet_aton("256.1.1.1", &a) == 0);
    printf("addr-bad %d\n", inet_addr("not an address") == INADDR_NONE);

    printf("network %d\n", inet_network("192.168.1.2") == 0xc0a80102u);
    a = inet_makeaddr(0xc0a801, 2);
    printf("makeaddr %s\n", inet_ntoa(a));
    printf("netof %d\n", inet_netof(a) == 0xc0a801);

    printf("pton4 %d\n", inet_pton(AF_INET, "10.0.0.1", &a) == 1);
    printf("ntop4 %s\n", inet_ntop(AF_INET, &a, buf, sizeof buf));
    printf("pton4-bad %d\n", inet_pton(AF_INET, "10.1", &a) == 0);

    printf("pton6 %d\n",
           inet_pton(AF_INET6, "2001:0db8:0:0:0:0:0:1", &a6) == 1);
    printf("ntop6 %s\n", inet_ntop(AF_INET6, &a6, buf, sizeof buf));
    printf("pton6-v4map %d\n",
           inet_pton(AF_INET6, "::ffff:1.2.3.4", &a6) == 1);
    printf("ntop6-v4map %s\n", inet_ntop(AF_INET6, &a6, buf, sizeof buf));
    printf("pton6-bad %d\n", inet_pton(AF_INET6, ":::", &a6) == 0);
    return 0;
}
