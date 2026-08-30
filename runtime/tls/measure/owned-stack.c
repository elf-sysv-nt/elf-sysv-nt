/*
 * owned-stack.c -- confirm a runtime-allocated thread stack makes
 * NtTib.StackBase reflect the allocation, that its bottom is a safe owned
 * carrier slot below the _cygtls reservation, and that fork works from it.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/cygwin.h>

#define TEB_STACKBASE 0x08
#define TEB_STACKLIMIT 0x10
static inline uint64_t gs_read(unsigned off){uint64_t v;
	__asm__ __volatile__("movq %%gs:(%1),%0":"=r"(v):"r"((uint64_t)off));return v;}
static inline uint64_t rsp(void){uint64_t v;
	__asm__ __volatile__("movq %%rsp,%0":"=r"(v));return v;}

#define STK (256*1024)
static void *g_stack;

static void *body(void *arg){
	(void)arg;
	uint64_t base=gs_read(TEB_STACKBASE), lim=gs_read(TEB_STACKLIMIT), sp=rsp();
	uint64_t pad=(uint64_t)cygwin_internal(CW_CYGTLS_PADSIZE);
	uint64_t lo=(uint64_t)(uintptr_t)g_stack, hi=lo+STK;
	printf("alloc=0x%llx..0x%llx\n",(unsigned long long)lo,(unsigned long long)hi);
	printf("stackbase=0x%llx in_alloc=%d\n",(unsigned long long)base,
		base>lo && base<=hi);
	printf("stacklimit=0x%llx rsp=0x%llx pad=%llu\n",(unsigned long long)lim,
		(unsigned long long)sp,(unsigned long long)pad);
	/* carrier at bottom of my allocation: StackBase-(STK-16) region */
	uint64_t carrier = base-(STK-16);
	printf("carrier=0x%llx below_cygtls=%d above_alloc_lo=%d far_below_rsp=%d\n",
		(unsigned long long)carrier, carrier < base-pad,
		carrier>=lo, carrier < sp);
	*(volatile uint64_t*)carrier = 0xC0FFEE;   /* prove writable */
	printf("carrier_write=ok readback=0x%llx\n",
		(unsigned long long)*(volatile uint64_t*)carrier);
	pid_t p=fork();
	if(p==0){ uint64_t cb=gs_read(TEB_STACKBASE);
		printf("child.stackbase=0x%llx same=%d carrier=0x%llx\n",
			(unsigned long long)cb,cb==base,
			(unsigned long long)*(volatile uint64_t*)(cb-(STK-16)));
		fflush(stdout); _exit(0);}
	int st; waitpid(p,&st,0);
	printf("fork_from_thread=ok\n");
	return NULL;
}
int main(void){
	g_stack=mmap(NULL,STK,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
	if(g_stack==MAP_FAILED){perror("mmap");return 1;}
	pthread_attr_t a; pthread_attr_init(&a);
	if(pthread_attr_setstack(&a,g_stack,STK)!=0){perror("setstack");return 1;}
	pthread_t t; if(pthread_create(&t,&a,body,NULL)!=0){perror("create");return 1;}
	pthread_join(t,NULL); pthread_attr_destroy(&a);
	return 0;
}
