#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include <stdint.h>
#include <stdbool.h>
#include <debug.h>

typedef int pid_t;
#define PID_ERROR ((pid_t) -1)

struct lock filesys_lock;

void syscall_init (void);

void check_address ( void *addr );

void sys_halt (void);
void sys_exit (int64_t status);
pid_t sys_exec (const char *cmd_line);
int sys_wait (pid_t pid);
bool sys_create (const char *file, unsigned initial_size);
bool sys_remove (const char *file);
int sys_open (const char *file);
int sys_filesize (int fd);
int sys_read (int fd, void *buffer, unsigned size);
int sys_write (int fd, void *buffer, unsigned size);
void sys_seek (int fd, unsigned position);
unsigned sys_tell (int fd);
void sys_close (int fd);


#endif /* userprog/syscall.h */
