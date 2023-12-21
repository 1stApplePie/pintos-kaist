#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/inode.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/thread.h"
#include "threads/loader.h"
#include "userprog/gdt.h"
#include "userprog/process.h"
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

enum {
	STD_INPUT,
	STD_OUTPUT,
	STD_ERROR
};

/* An open file. */
struct file {
	struct inode *inode;        /* File's inode. */
	off_t pos;                  /* Current position. */
	bool deny_write;            /* Has file_deny_write() been called? */
};

static void
check_address (void *addr) {
	if(!is_user_vaddr(addr) || addr == NULL)
		exit(-1);
}

static void
check_pt(void *addr) {
	if (pml4_get_page(thread_current()->pml4, addr) == NULL) {
		exit(-1);
	}
}

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
	
	sema_init(&mutex, 1);
}

/* The main system call interface */
void
syscall_handler (struct intr_frame *f UNUSED) {
	// 유저 프로그램 실행 정보는 syscall_handler로 전달되는 intr_frame에 저장
	thread_current()->user_rsp = f->rsp;

 	switch (f->R.rax) {
    case SYS_HALT:
	{
		halt();
		break;
	}

    case SYS_EXIT:
	{
		exit (f->R.rdi);
        break;
	}

    case SYS_FORK:
	{
		f->R.rax = fork (f->R.rdi, f);
		break;
	}

	case SYS_EXEC:
	{
		f->R.rax = exec (f->R.rdi);
        break;
	}

	case SYS_WAIT:
	{
		f->R.rax = wait (f->R.rdi);
		break;
	}

    case SYS_CREATE:
	{
		check_address(f->R.rdi);
		check_pt(f->R.rdi);
		f->R.rax = create (f->R.rdi, f->R.rsi);
		break;
	}

	case SYS_REMOVE:
	{
		check_address(f->R.rdi);
		f->R.rax = remove (f->R.rdi);
		break;
	}

	case SYS_OPEN:
	{
		check_address(f->R.rdi);
		check_pt(f->R.rdi);
		f->R.rax = open (f->R.rdi);
		break;
	}
        	
	case SYS_FILESIZE:
	{
		f->R.rax = filesize (f->R.rdi);
        break;
	}

	case SYS_READ:
	{
		check_address (f->R.rsi);
		f->R.rax = read (f->R.rdi, f->R.rsi, f->R.rdx);
        break;
	}

	case SYS_WRITE:
	{
		f->R.rax = write (f->R.rdi, f->R.rsi, f->R.rdx);
		break;
	}

	case SYS_SEEK:
	{
		seek (f->R.rdi, f->R.rsi);
        break;
	}

	case SYS_TELL:
	{
		f->R.rax = tell (f->R.rdi);
		break;
	}
        

	case SYS_CLOSE:
	{
		close(f->R.rdi);
		break;
	}
        

	case SYS_DUP2:
        break;

    default:
        break;
}
}

/*
	* Terminates Pintos by calling power_off() 
	* (declared in src/include/threads/init.h).
	* This should be seldom used, 
	* because you lose some information about possible deadlock situations, etc.
*/
void halt(void) {
	power_off();
}

/*
	* Terminates the current user program, 
	* returning status to the kernel. 
	* If the process's parent waits for it (see below), 
	* this is the status that will be returned. Conventionally, 
	* a status of 0 indicates success and nonzero values indicate errors.
*/
void
exit (int status) {
	struct thread *curr = thread_current();
	curr->exit_status = status;
	printf("%s: exit(%d)\n", thread_name(), status);
	thread_exit();
}

/*
	* Create new process which is the clone of current process 
	* with the name THREAD_NAME
*/
pid_t
fork (const char *thread_name, struct intr_frame *f){
	return (pid_t)process_fork(thread_name, f);
}

/*
	지정된 인수를 전달하여 현재 프로세스를 cmd_line에 지정된 이름의 실행 파일로 변경합니다. 
	성공하면 절대 반환되지 않습니다. 
	그렇지 않으면 어떤 이유로든 프로그램을 로드하거나 
	실행할 수 없는 경우 종료 상태 -1로 프로세스가 종료됩니다. 
	이 함수는 exec를 호출한 스레드의 이름을 변경하지 않습니다. 
	파일 설명자는 실행 호출 내내 열려 있다는 점에 유의하세요.
*/
int
exec (const char *cmd_line) {
	if (cmd_line == NULL) {
		thread_current()->exit_status = -1;
		thread_exit();
	}

	void *cmd_copy;
    cmd_copy = palloc_get_page(PAL_ZERO);
    if (cmd_copy == NULL)
        return -1;
	
	strlcpy(cmd_copy, cmd_line, PGSIZE);

	return process_exec(cmd_copy);
}

/*
	자식 프로세스 pid를 기다렸다가 자식의 종료 상태를 검색합니다.
	pid가 아직 살아있다면 종료될 때까지 기다립니다. 
	그런 다음 pid가 종료하기 위해 전달한 상태를 반환합니다. 
	pid가 exit()를 호출하지 않았지만 커널에 의해 종료된 경우(예: 예외로 인해 종료된 경우), 
	wait(pid)는 -1을 반환해야 합니다. 
	부모 프로세스가 wait를 호출할 때 이미 종료된 자식 프로세스를 기다리는 것은 완전히 합법적이지만, 
	커널은 여전히 부모가 자식의 종료 상태를 검색하거나 
	자식이 커널에 의해 종료되었음을 알 수 있도록 허용해야 합니다.
*/
int
wait (pid_t pid) {
	return process_wait(pid);
}

/*
	초기 initial_size 바이트 크기의 file이라는 새 파일을 생성합니다. 
	성공하면 참을 반환하고, 그렇지 않으면 거짓을 반환합니다. 
	새 파일을 만든다고 해서 파일이 열리지는 않습니다. 
	새 파일을 열려면 시스템 호출이 필요한 별도의 작업입니다.
*/
bool
create (const char *file, unsigned initial_size) {
	
	if (file == NULL || initial_size<0) {
		exit(-1);
	}
	return filesys_create(file, initial_size);
}

/*
	file이라는 파일을 삭제합니다. 
	성공하면 참을 반환하고, 그렇지 않으면 거짓을 반환합니다. 
	파일은 열려 있는지 여부에 관계없이 제거할 수 있으며 열려 있는 파일을 제거해도 닫히지 않습니다. 
	자세한 내용은 FAQ에서 열린 파일 제거하기를 참조하세요.
*/
bool
remove (const char *file) {
	return filesys_remove(file);
}

/*
	Opens the file called file. Returns a nonnegative integer handle called a "file descriptor" (fd),
	or -1 if the file could not be opened. 
	File descriptors numbered 0 and 1 are reserved for the console:
	fd 0 (STDIN_FILENO) is standard input, fd 1 (STDOUT_FILENO) is standard output. 
	The open system call will never return either of these file descriptors, 
	which are valid as system call arguments only as explicitly described below. 
	Each process has an independent set of file descriptors. 
	File descriptors are inherited by child processes. 
	When a single file is opened more than once, 
	whether by a single process or different processes, 
	each open returns a new file descriptor. 
	Different file descriptors for a single file are closed independently 
	in separate calls to close and they do not share a file position.
*/
int
open (const char *file) {
	if (file == NULL) {
		return -1;
	}

	struct file* opened_f = filesys_open(file);
	if (opened_f == NULL) {
		return -1;
	}

	struct thread *curr = thread_current();

	if (curr->fd_idx > FD_MAX) {
		file_close(opened_f);
		return -1;
	}

	curr->fd_table[curr->fd_idx++] = opened_f;

	return curr->fd_idx-1;
}

/*
	Returns the size, in bytes, of the file open as fd.
*/
int
filesize (int fd) {
	struct thread *curr = thread_current();
	struct file* opened_f = curr->fd_table[fd];

	if (opened_f == NULL) {
		return -1;
	}

	return file_length(opened_f);
}

/*
	Reads size bytes from the file open as fd into buffer.
	Returns the number of bytes actually read (0 at end of file), 
	or -1 if the file could not be read (due to a condition other than end of file). 
*/
int
read (int fd, void *buffer, unsigned size) {
	if (fd < 0 || fd == 1)
		return -1;
		
	struct thread *curr = thread_current();
	struct file* opened_f = curr->fd_table[fd];

	sema_down(&mutex);
	
	if (opened_f == NULL) {
		sema_up(&mutex);
		return -1;
	}
	
	// fd 0 reads from the keyboard using input_getc()
	else if (fd == 0) {
        char *buf_pos = (char *)buffer;
        while ((buf_pos - (char *)buffer) < size - 1) {
            *buf_pos = input_getc();
            if (*buf_pos == '\0' || *buf_pos == '\n') {
                break;
            }
            buf_pos += 1;
        }
        *buf_pos = '\0';
		sema_up(&mutex);
		return buf_pos - (char *)buffer;
    }

	else if (fd >= 2) {
		off_t res = file_read(opened_f, buffer, size);
		sema_up(&mutex);
		return res;
	}

	sema_up(&mutex);
	return -1;
}

/*
	Writes size bytes from buffer to the open file fd. 
	Returns the number of bytes actually written, 
	which may be less than size if some bytes could not be written. 
	Writing past end-of-file would normally extend the file, 
	but file growth is not implemented by the basic file system. 
	The expected behavior is to write as many bytes as possible up to end-of-file 
	and return the actual number written, or 0 if no bytes could be written at all. 
	fd 1 writes to the console. Your code to write to the console should 
	write all of buffer in one call to putbuf(), at least as long as size 
	is not bigger than a few hundred bytes (It is reasonable to break up larger buffers).
	Otherwise, lines of text output by different processes may end up interleaved on the console, 
	confusing both human readers and our grading scripts.
*/
int
write (int fd, const void *buffer, unsigned size) {
	// Error - invalid fd
	if (fd < 0 || fd > FD_MAX)
		return NULL;
	
	sema_down(&mutex);

	if (fd == STD_OUTPUT) {
		putbuf(buffer, size);
		sema_up(&mutex);
		return size;
	}

	else {
		struct thread *curr = thread_current();
		struct file* opened_f = curr->fd_table[fd];

		if (opened_f == NULL) {
			sema_up(&mutex);
			return 0;
		}
		off_t res = file_write(opened_f, buffer, size);
		sema_up(&mutex);
		return res;
	}

	sema_up(&mutex);
	return 0;
}

/*
	Changes the next byte to be read or written in open file fd to position, 
	expressed in bytes from the beginning of the file (Thus, a position of 0 is the file's start).
	A seek past the current end of a file is not an error. 
	A later read obtains 0 bytes, indicating end of file. 
	A later write extends the file, filling any unwritten gap with zeros.
	(However, in Pintos files have a fixed length until project 4 is complete, 
	so writes past end of file will return an error.) 
	These semantics are implemented in the file system and
	do not require any special effort in system call implementation.
*/
void
seek (int fd, unsigned position) {
	struct thread *curr = thread_current();
	struct file *opened_f = curr->fd_table[fd];

	file_seek(opened_f, position);
}


/*
	Returns the position of the next byte to be read or written in open file fd, 
	expressed in bytes from the beginning of the file.
*/
unsigned
tell (int fd) {
	struct thread *curr = thread_current();
	struct file *opened_f = curr->fd_table[fd];

	return file_tell(opened_f);
}

/*
	Closes file descriptor fd. Exiting or terminating a process 
	implicitly closes all its open file descriptors, 
	as if by calling this function for each one.
*/
void
close (int fd) {
	// Error - invalid fd
	if (fd < 0 || fd > FD_MAX)
		return NULL;

	struct thread *curr = thread_current();
	struct file *file_obj = curr->fd_table[fd];

	if (file_obj == NULL) {
		return;
	}
	else {
		file_close(file_obj);
		curr->fd_table[fd] = NULL;
	}
}

/*
*/
// int
// dup2 (int oldfd, int newfd){
// 	return syscall2 (SYS_DUP2, oldfd, newfd);
// }

// void *
// mmap (void *addr, size_t length, int writable, int fd, off_t offset) {
// 	return (void *) syscall5 (SYS_MMAP, addr, length, writable, fd, offset);
// }

// void
// munmap (void *addr) {
// 	syscall1 (SYS_MUNMAP, addr);
// }

// bool
// chdir (const char *dir) {
// 	return syscall1 (SYS_CHDIR, dir);
// }

// bool
// mkdir (const char *dir) {
// 	return syscall1 (SYS_MKDIR, dir);
// }

// bool
// readdir (int fd, char name[READDIR_MAX_LEN + 1]) {
// 	return syscall2 (SYS_READDIR, fd, name);
// }

// bool
// isdir (int fd) {
// 	return syscall1 (SYS_ISDIR, fd);
// }

// int
// inumber (int fd) {
// 	return syscall1 (SYS_INUMBER, fd);
// }

// int
// symlink (const char* target, const char* linkpath) {
// 	return syscall2 (SYS_SYMLINK, target, linkpath);
// }

// int
// mount (const char *path, int chan_no, int dev_no) {
// 	return syscall3 (SYS_MOUNT, path, chan_no, dev_no);
// }

// int
// umount (const char *path) {
// 	return syscall1 (SYS_UMOUNT, path);
// }
