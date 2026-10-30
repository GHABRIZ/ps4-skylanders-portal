/* Minimal stand-in for <orbis/libkernel.h> so main.c can be run on a PC by test/host_sim.c. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <pthread.h>

typedef uint32_t OrbisKernelModule;
typedef struct { void *address; uint32_t size; int32_t prot; } OrbisKernelModuleSegmentInfo;
typedef struct { size_t size; char name[256]; OrbisKernelModuleSegmentInfo segmentInfo[4]; uint32_t segmentCount; uint8_t fingerprint[20]; } OrbisKernelModuleInfo;
typedef struct { void *start_addr; void *end_addr; off_t offset; int32_t prot; int32_t memory_type; uint8_t flags[4]; char name[32]; } OrbisKernelVirtualQueryInfo;
typedef pthread_t OrbisPthread;
typedef pthread_attr_t OrbisPthreadAttr;

int sceKernelOpen(const char *, int, int);
int64_t sceKernelRead(int, void *, size_t);
int64_t sceKernelWrite(int, const void *, size_t);
int sceKernelClose(int);
int sceKernelMkdir(const char *, int);
int sceKernelUnlink(const char *);
int sceKernelGetdents(int, char *, int);
int sceKernelUsleep(unsigned);
int sceKernelGetModuleInfo(OrbisKernelModule, OrbisKernelModuleInfo *);
int sceKernelGetModuleList(OrbisKernelModule *, size_t, size_t *);
int sceKernelDlsym(int, const char *, void **);
int sceKernelVirtualQuery(const void *, int, OrbisKernelVirtualQueryInfo *, size_t);
int sceKernelMprotect(const void *, size_t, int);
int scePthreadCreate(OrbisPthread *, const OrbisPthreadAttr *, void *(*)(void *), void *, const char *);
int scePthreadAttrInit(OrbisPthreadAttr *);
int scePthreadAttrSetstacksize(OrbisPthreadAttr *, size_t);
int scePthreadAttrDestroy(OrbisPthreadAttr *);
int scePthreadAttrSetdetachstate(OrbisPthreadAttr *, int);
