#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "threads/flags.h"
#include "userprog/process.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
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

	lock_init(&filesys_lock); // 파일 관련 lock 초기화
}

// 주소 값이 유저 영역에서 사용하는 주소 값인지 확인
void check_address ( void *addr ) {
	struct thread * t = thread_current();

	/* 인자로 받아온 주소가 유저영역의 주소가 아니거나 , 주소가 NULL이거나  해당 페이지가 존재하지 않을경우 프로그램 종료 */
	if ( addr == NULL || is_user_vaddr(addr) == false || pml4_get_page(t->pml4, addr)== NULL ) {
		sys_exit(-1);
	}
		
}

void sys_halt (void) {
	/* Shut down PintOS */
	power_off();
}

pid_t sys_fork (const char *thread_name, struct intr_frame *if_) {
	return process_fork(thread_name, if_);
}

void sys_exit (int64_t status) {
	struct thread *t = thread_current();

	t -> is_exit = true; // 정적으로 종료했는지 여부를 판단하기 위해 is_exit값 저장;
	t -> exit_status = status;
	
	printf("%s: exit(%d)\n" , t -> name , status);

	thread_exit();
}

tid_t sys_exec (const char *cmd_line) {
	tid_t pid = process_exec (cmd_line); // prcess_exec를 통해 자식 프로세스 생성

	struct thread *child_thread = get_child_process(pid); // 자식 프로세스 탐색

	if (child_thread == NULL)
		return -1;
	
	sema_down (&child_thread -> load_sema); // 자식 프로세스가 Load 될때 까지 대기

	if (child_thread -> is_load) // 자식 프로세스가 Load 되었을 때 Pid를 return
		return pid;

	return -1;
}

int sys_wait (pid_t pid) {
	return process_wait(pid);
}

bool sys_create (const char *file, unsigned initial_size) {
	bool success;

	check_address(file);

	if (file == NULL) {
		return false;
	}

	lock_acquire(&filesys_lock); // 파일 동시접근을 제어하기 위해 LOCK 사용
	success = filesys_create(file, initial_size);
	lock_release(&filesys_lock); // Lock 해제

	return success;
}

bool sys_remove (const char *file) {
	bool res;

	lock_acquire(&filesys_lock); // 파일 동시접근을 제어하기 위해 LOCK 사용
	res = filesys_remove(file);
	lock_release(&filesys_lock); // Lock 해제

	return res;
}

int sys_open (const char *file) {
	struct file *f;

	check_address(file);

	if (file == "" || file == NULL) // tests/userprog/open-bad-ptr
		return -1;
	
	lock_acquire(&filesys_lock); // 파일 동시접근을 제어하기 위해 LOCK 사용
	f = filesys_open(file);
	lock_release(&filesys_lock); // Lock 해제

	if (f == NULL)
		return -1;

	int fd = process_add_file(f);

	if (fd == -1)
		file_close(f);

	return fd;
}

int sys_filesize (int fd) {
	struct thread *t = thread_current();
	struct file* f = t -> fd_table[fd];

	if (f != NULL)
		return file_length(f);

	return -1;
}

int sys_read (int fd, void *buffer, unsigned size) {
	// 열린 파일의 데이터를 읽는 시스템 콜
	int readed_byte;

	if (fd == 0) { // 표준 입력 FD (0)을 전달 받은 경우
		unsigned i;
		for (i = 0; i < size; i++) { // 최대 size 만큼 문자를 입력받는다.
				((uint8_t *)buffer)[i] = input_getc();
				if (((uint8_t *)buffer)[i] == '\0') {
						break;
				}
		}
		readed_byte = i; // 입력한 사이즈에 대해서 할당

	} else { // 표준 입출력 FD (0) 이외의 값을 전달 받은 경우
		lock_acquire(&filesys_lock); // 파일 동시접근을 제어하기 위해 LOCK 사용

		struct file *f = process_get_file(fd); // 파일 디스크립터를 통한 파일 객체 검색

		if (f == NULL) { // 파일이 없는 경우 예외처리
			lock_release(&filesys_lock); // Lock 해제
			return -1;
		}

		readed_byte = file_read(f, buffer, size); // 파일의 데이터를 size만큼 저장하고 읽은 바이트 수 리턴
		
		lock_release(&filesys_lock); // Lock 해제
	}

	return readed_byte;
}

int sys_write (int fd, void *buffer, unsigned size) {
	check_address(buffer);

	int write_byte = -1;

	if (fd == 1) {  // 표준 출력 FD (1) 을 전달받은 경우
		putbuf(buffer, size); // 버퍼에 저장된 값을 화면에 출력후 

		write_byte = size; // 버퍼의 크기 반환

	} else {  // 표준 출력 FD (1)외의 FD를 전달받은 경우
		lock_acquire(&filesys_lock); // 파일 동시 접근을 막기 위해 Lock 사용

		struct file *f = process_get_file(fd); // 파일 디스크립터를 통한 파일 객체 검색

		if (f != NULL) { // 파일이 없는 경우 예외 처리
			write_byte = file_write(f, buffer, size); // 버퍼에 저장된 크기만큼 파일에 기록한 바이트 수 리턴
		}

		lock_release(&filesys_lock); // Lock 해제 
	}

	return write_byte;
}

void sys_seek ( int fd, unsigned position ) {
	struct thread *t = thread_current();
	struct file *f = t -> fd_table[fd];

	if (f != NULL) {
		file_seek(f, position); // 파일 위치를 설정
	}
}

unsigned sys_tell ( int fd ) {
	struct thread *t = thread_current();
	struct file *f = t -> fd_table[fd];

	return file_tell(f);
}

void sys_close (int fd) {
	process_close_file(fd);
}


/* The main system call interface */
void
syscall_handler (struct intr_frame *f) {
	// 시스템 호출 번호와 인자들을 저장할 변수들
	int call_num_ptr = f->R.rax;
	
	switch (call_num_ptr) {
		case SYS_HALT:                 /* Halt the operating system. */
			sys_halt();
			break;

		case SYS_FORK:									/* Clone current process. */
			f -> R.rax = sys_fork((char *)f -> R.rdi, f); 
			break;

		case SYS_EXIT:                /* Terminate this process. */
			sys_exit(f -> R.rdi);
			break;    

		case SYS_EXEC:                /* Switch current process. */
			f -> R.rax = sys_exec(f -> R.rdi);
			break; 

		case SYS_WAIT:                /* Wait for a child process to die. */
			f -> R.rax = sys_wait(f -> R.rdi);
			break; 

		case SYS_CREATE:              /* Create a file. */
			f -> R.rax = sys_create(f -> R.rdi, f -> R.rsi);
			break;

		case SYS_REMOVE:              /* Delete a file. */
			f -> R.rax = sys_remove(f -> R.rdi);
			break;

		case SYS_OPEN:                /* Open a file. */
			f -> R.rax = sys_open(f -> R.rdi);
			break;
			
		case SYS_FILESIZE:            /* Obtain a file's size. */
			f -> R.rax = sys_filesize(f -> R.rdi);
			break;
		
		case SYS_READ:                /* Read from a file. */
			f -> R.rax = sys_read(f -> R.rdi, f -> R.rsi, f -> R.rdx);
			break;

		case SYS_WRITE:              /* Write to a file. */
			f -> R.rax = sys_write(f -> R.rdi, f -> R.rsi, f -> R.rdx);
			break;

		case SYS_SEEK:                /* Change position in a file. */
			sys_seek(f -> R.rdi, f -> R.rsi);
			break;

		case SYS_TELL:                /* Report current position in a file. */
			f -> R.rax = sys_tell(f -> R.rdi);
			break;

		case SYS_CLOSE:               /* Close a file. */
			sys_close(f -> R.rdi);
			break;
		
		default:
			thread_exit();
			break;
	}
}
