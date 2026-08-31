/* sockets slice: name resolution without a resolver -- getaddrinfo
   under AI_NUMERICHOST/AI_NUMERICSERV, getnameinfo turning it back,
   the error strings, all network-free. */
#define _GNU_SOURCE
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

int main(void)
{
    struct addrinfo hints, *res;
    char host[NI_MAXHOST], serv[NI_MAXSERV];
    int rc;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    rc = getaddrinfo("127.0.0.1", "8080", &hints, &res);
    printf("gai4 %d\n", rc == 0);
    printf("gai4-one %d\n", rc == 0 && res != NULL && res->ai_next == NULL);
    printf("gai4-shape %d\n", rc == 0 &&
           res->ai_family == AF_INET &&
           res->ai_socktype == SOCK_STREAM &&
           res->ai_addrlen == sizeof(struct sockaddr_in));

    if (rc == 0) {
        rc = getnameinfo(res->ai_addr, res->ai_addrlen,
                         host, sizeof host, serv, sizeof serv,
                         NI_NUMERICHOST | NI_NUMERICSERV);
        printf("gni4 %d %s %s\n", rc == 0, host, serv);
        freeaddrinfo(res);
    }

    hints.ai_family = AF_INET6;
    rc = getaddrinfo("::1", "443", &hints, &res);
    printf("gai6 %d\n", rc == 0);
    if (rc == 0) {
        rc = getnameinfo(res->ai_addr, res->ai_addrlen,
                         host, sizeof host, NULL, 0, NI_NUMERICHOST);
        printf("gni6 %d %s\n", rc == 0, host);
        freeaddrinfo(res);
    }

    /* a name that is not numeric fails under AI_NUMERICHOST */
    hints.ai_family = AF_UNSPEC;
    rc = getaddrinfo("localhost.invalid", NULL, &hints, &res);
    printf("gai-noname %d\n", rc == EAI_NONAME);
    printf("gai-strerror %d\n",
           gai_strerror(EAI_NONAME) != NULL &&
           strlen(gai_strerror(EAI_NONAME)) > 0);
    printf("hstrerror %d\n",
           hstrerror(HOST_NOT_FOUND) != NULL &&
           strlen(hstrerror(HOST_NOT_FOUND)) > 0);
    return 0;
}
