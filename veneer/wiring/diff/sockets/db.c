/* sockets slice: the services and protocols databases -- lookups by
   name, port and number against the well-known entries both sides'
   /etc files carry. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <netdb.h>

int main(void)
{
    struct servent *s;
    struct protoent *p;

    s = getservbyname("http", "tcp");
    printf("http %d\n", s != NULL && ntohs(s->s_port) == 80);
    s = getservbyname("ssh", "tcp");
    printf("ssh %d\n", s != NULL && ntohs(s->s_port) == 22);
    s = getservbyport(htons(53), "udp");
    printf("port53 %d\n", s != NULL && strcmp(s->s_name, "domain") == 0);
    s = getservbyname("no-such-service-here", "tcp");
    printf("no-serv %d\n", s == NULL);

    p = getprotobyname("tcp");
    printf("tcp %d\n", p != NULL && p->p_proto == 6);
    p = getprotobyname("udp");
    printf("udp %d\n", p != NULL && p->p_proto == 17);
    p = getprotobynumber(1);
    printf("proto1 %d\n", p != NULL && strcmp(p->p_name, "icmp") == 0);
    p = getprotobyname("no-such-proto");
    printf("no-proto %d\n", p == NULL);

    /* the enumerators open, yield something, rewind, and close */
    setservent(1);
    s = getservent();
    printf("servent %d\n", s != NULL && s->s_name != NULL);
    endservent();

    setprotoent(1);
    p = getprotoent();
    printf("protoent %d\n", p != NULL && p->p_name != NULL);
    endprotoent();

    sethostent(1);
    endhostent();
    printf("hostent ok\n");
    return 0;
}
