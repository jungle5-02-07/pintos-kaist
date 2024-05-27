#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#define SYS_HALT 0  // 시스템 종료를 위한 시스템 콜 번호

void syscall_init (void);
void halt(void);

#endif /* userprog/syscall.h */
