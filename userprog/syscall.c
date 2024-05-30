#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "intrinsic.h"

void syscall_entry (void);
void syscall_handler (struct intr_frame *);

/* System call.
 *
 * Previously system call services was handled by the interrupt handler
 * (e.g. int 0x80 in linux). However, in x86-64, the manufacturer supplies
 * efficient path for requesting the system call, the `syscall` instruction.
 *
 * The syscall instruction works by reading the values from the the Model
 * Specific Register (MSR). For the details, see the manual. */

#define MSR_STAR 0xc0000081         /* Segment selector msr */
#define MSR_LSTAR 0xc0000082        /* Long mode SYSCALL target */
#define MSR_SYSCALL_MASK 0xc0000084 /* Mask for the eflags */

void
syscall_init (void) {
	write_msr(MSR_STAR, ((uint64_t)SEL_UCSEG - 0x10) << 48  |
			((uint64_t)SEL_KCSEG) << 32);
	write_msr(MSR_LSTAR, (uint64_t) syscall_entry);

	/* The interrupt service rountine should not serve any interrupts
	 * until the syscall_entry swaps the userland stack to the kernel
	 * mode stack. Therefore, we masked the FLAG_FL. */
	write_msr(MSR_SYSCALL_MASK,
			FLAG_IF | FLAG_TF | FLAG_DF | FLAG_IOPL | FLAG_AC | FLAG_NT);
}

void halt(void){
	power_off();
}

void exit(int status){
	struct thread *cur = thread_current();
	cur->exit_status = status;
	printf("%s: exit(%d)\n",);
	thread_exit();
}

pid_t fork(const char *thread_name){

}

int exec(const char *cmd_line){

}

int wait(pid_t pid){

}

bool create(const char *file, unsigned initial_size)
{
	return filesys_create(file, initial_size);
}

bool remove(const char *file){
	return filesys_remove(file);
}

int open(const char *file){

}

int filesize(int fd){

}

int read(int fd, void *buffer, unsigned size){

}

int write(int fd, const void *buffer, unsigned size){

}

void seek(int fd, unsigned position){

}

unsigned tell(int fd){

}

void close(int fd){

}

/* The main system call interface */
void syscall_handler (struct intr_frame *f UNUSED) {
	// TODO: Your implementation goes here.
	int syscall_n = f->R.rax;
	switch(syscall_n){
		case SYS_HALT:
			halt();
			break;

		case SYS_EXIT:
			exit(f->R.rdi);
			break;

		case SYS_FORK:
			break;

		case SYS_EXEC:
			break;

		case SYS_WAIT:
			break;
		
		case SYS_CREATE:
			break;

		case SYS_REMOVE:
			break;

		case SYS_OPEN:
			break;

		case SYS_FILESIZE:
			break;
		
		case SYS_READ:
			break;

		case SYS_WRITE:
			break;

		case SYS_SEEK:
			break;
		
		case SYS_TELL:
			break;

		case SYS_CLOSE:
			break;
		}

}
