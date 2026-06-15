# Mellivora Syscall Catalog

This file is generated from programs/syscalls.inc.
Do not edit manually; run: python3 tools/generate_syscalls_json.py

- Source: programs/syscalls.inc
- Syscall count: 182

| Number | Name | Summary |
| ---: | --- | --- |
| 0 | SYS_EXIT | - |
| 1 | SYS_PUTCHAR | - |
| 2 | SYS_GETCHAR | - |
| 3 | SYS_PRINT | - |
| 4 | SYS_READ_KEY | - |
| 5 | SYS_OPEN | - |
| 6 | SYS_READ | - |
| 7 | SYS_WRITE | - |
| 8 | SYS_CLOSE | - |
| 9 | SYS_DELETE | - |
| 10 | SYS_SEEK | - |
| 11 | SYS_STAT | - |
| 12 | SYS_MKDIR | Create directory: EBX=name -> EAX=0 success |
| 13 | SYS_READDIR | Read directory entry: EBX=buf ECX=index -> EAX=type(-1=end) ECX=size |
| 14 | SYS_SETCURSOR | - |
| 15 | SYS_GETTIME | - |
| 16 | SYS_SLEEP | - |
| 17 | SYS_CLEAR | - |
| 18 | SYS_SETCOLOR | - |
| 19 | SYS_MALLOC | - |
| 20 | SYS_FREE | - |
| 21 | SYS_EXEC | - |
| 22 | SYS_DISK_READ | Raw disk read (kernel-only; returns -1 from user programs) |
| 23 | SYS_SBRK | Adjust program break: EBX=increment -> EAX=old_brk/-1 |
| 24 | SYS_BEEP | PC speaker beep: EBX=frequency_hz ECX=duration_ticks (1 tick=10ms at 100Hz) |
| 25 | SYS_DATE | - |
| 26 | SYS_CHDIR | - |
| 27 | SYS_GETCWD | - |
| 28 | SYS_SERIAL | - |
| 29 | SYS_GETENV | - |
| 30 | SYS_FREAD | Read entire file: EBX=name ECX=buf -> EAX=bytes |
| 31 | SYS_FWRITE | Write file: EBX=name ECX=buf EDX=size ESI=type(0=text) |
| 32 | SYS_GETARGS | Get command-line args: EBX=buf -> EAX=length |
| 33 | SYS_SERIAL_IN | Read serial port: -> EAX=char or -1 |
| 34 | SYS_STDIN_READ | Read piped stdin: EBX=buf -> EAX=bytes (-1 if none) |
| 35 | SYS_YIELD | Cooperative yield: switch to next ready task |
| 36 | SYS_MOUSE | Read mouse: -> EAX=x, EBX=y, ECX=buttons |
| 37 | SYS_FRAMEBUF | Framebuffer: EBX=sub (0=info,1=set,2=restore,3=text,4=present,5=surfbuf) |
| 38 | SYS_GUI | Burrows GUI: EBX=sub-function |
| 39 | SYS_SOCKET | Create socket: EBX=type(1=TCP,2=UDP) -> EAX=fd |
| 40 | SYS_CONNECT | Connect: EBX=fd ECX=ip EDX=port -> EAX=0/-1 |
| 41 | SYS_SEND | Send: EBX=fd ECX=buf EDX=len -> EAX=bytes |
| 42 | SYS_RECV | Recv: EBX=fd ECX=buf EDX=maxlen -> EAX=bytes |
| 43 | SYS_BIND | Bind: EBX=fd ECX=port -> EAX=0/-1 |
| 44 | SYS_LISTEN | Listen: EBX=fd -> EAX=0/-1 |
| 45 | SYS_ACCEPT | Accept: EBX=fd -> EAX=new_fd |
| 46 | SYS_DNS | DNS: EBX=hostname -> EAX=ip (0=fail) |
| 47 | SYS_SOCKCLOSE | Close socket: EBX=fd -> EAX=0 |
| 48 | SYS_PING | Ping: EBX=ip -> EAX=rtt/-1 |
| 49 | SYS_SETDATE | Set RTC: EBX=buf[sec,min,hr,day,mon,yr] ECX=century |
| 50 | SYS_AUDIO_PLAY | Play PCM: EBX=buf ECX=len EDX=fmt -> EAX=0/-1 |
| 51 | SYS_AUDIO_STOP | Stop playback -> EAX=0 |
| 52 | SYS_AUDIO_STATUS | Query: -> EAX=state(0/1/2) EBX=present |
| 53 | SYS_KILL | Kill task: EBX=pid -> EAX=0/-1 |
| 54 | SYS_GETPID | Get PID: -> EAX=pid |
| 55 | SYS_CLIPBOARD_COPY | Copy: EBX=buf ECX=len -> EAX=0 |
| 56 | SYS_CLIPBOARD_PASTE | Paste: EBX=buf ECX=maxlen -> EAX=len |
| 57 | SYS_NOTIFY | Notify: EBX=text EDX=color -> EAX=0 |
| 58 | SYS_FILE_OPEN_DLG | Open dialog: EBX=title EDX=filter -> EAX=1/0 ECX=name |
| 59 | SYS_FILE_SAVE_DLG | Save dialog: EBX=title EDX=filter -> EAX=1/0 ECX=name |
| 60 | SYS_PIPE_CREATE | Create pipe: -> EAX=pipe_id |
| 61 | SYS_PIPE_WRITE | Write pipe: EBX=id ECX=buf EDX=len -> EAX=written |
| 62 | SYS_PIPE_READ | Read pipe: EBX=id ECX=buf EDX=max -> EAX=read |
| 63 | SYS_PIPE_CLOSE | Close pipe: EBX=id -> EAX=0 |
| 64 | SYS_SHMGET | Get shared mem: EBX=key ECX=size -> EAX=shm_id |
| 65 | SYS_SHMADDR | Get shm address: EBX=shm_id -> EAX=ptr |
| 66 | SYS_PROCLIST | Get task info: EBX=slot ECX=buf(16B) -> EAX=0/-1 |
| 67 | SYS_MEMINFO | Get mem info: -> EAX=free_pages EBX=boot_free_pages |
| 68 | SYS_CHMOD | Change perms: EBX=filename ECX=perms -> EAX=0/-1 |
| 69 | SYS_CHOWN | Change owner: EBX=filename ECX=uid -> EAX=0/-1 |
| 70 | SYS_SYMLINK | Create link: EBX=linkname ECX=target -> EAX=0/-1 |
| 71 | SYS_READLINK | Read link: EBX=linkname ECX=buf -> EAX=len/-1 |
| 72 | SYS_SETPRIORITY | Set priority: EBX=pid(0=self) ECX=prio -> EAX=0/-1 |
| 73 | SYS_GETPRIORITY | Get priority: EBX=pid(0=self) -> EAX=prio/-1 |
| 74 | SYS_SIGNAL | Send signal: EBX=pid ECX=signum -> EAX=0/-1 |
| 75 | SYS_SETPGID | Set PGID: EBX=pid(0=self) ECX=pgid -> EAX=0/-1 |
| 76 | SYS_GETPGID | Get PGID: EBX=pid(0=self) -> EAX=pgid/-1 |
| 77 | SYS_SIGMASK | Signal mask: EBX=op ECX=mask -> EAX=old/-1 |
| 78 | SYS_TASKNAME | Set task name: EBX=name_ptr -> EAX=0 |
| 79 | SYS_REALLOC | Realloc: EBX=ptr ECX=new_size EDX=old_size -> EAX=ptr/0 |
| 80 | SYS_GETENV_SLOT | Get env slot: EBX=index ECX=dest_buf(128) -> EAX=0/-1 |
| 81 | SYS_DMESG_WRITE | Write dmesg: EBX=msg_ptr -> EAX=0 |
| 82 | SYS_DMESG_READ | Read dmesg: EBX=index ECX=dest_buf(128) -> EAX=0/-1 |
| 83 | SYS_RENAME | Rename file/dir: EBX=old_name ECX=new_name -> EAX=0/-1 |
| 84 | SYS_RMDIR | Remove empty dir: EBX=name -> EAX=0/-1 |
| 85 | SYS_TRUNCATE | Truncate file: EBX=name ECX=new_size -> EAX=0/-1 |
| 86 | SYS_CONNECT_NB | Non-blocking connect: EBX=fd ECX=ip EDX=port -> EAX=0/-2/-1 |
| 87 | SYS_POLL | Poll socket: EBX=fd ECX=events(1=in,2=out) EDX=timeout_ms -> EAX=ready_mask |
| 88 | SYS_SEM_CREATE | Create semaphore: EBX=initial_val -> EAX=sem_id/-1 |
| 89 | SYS_SEM_WAIT | Wait (P): EBX=sem_id -> EAX=0/-1 |
| 90 | SYS_SEM_POST | Post (V): EBX=sem_id -> EAX=0/-1 |
| 91 | SYS_SEM_CLOSE | Close semaphore: EBX=sem_id -> EAX=0/-1 |
| 92 | SYS_WAITPID | Wait for task: EBX=pid -> EAX=exit_code/-1 |
| 93 | SYS_GETMTIME | Get mtime: EBX=filename -> EAX=mtime ECX=ctime |
| 94 | SYS_SETMTIME | Set mtime: EBX=filename ECX=timestamp(0=now) -> EAX=0/-1 |
| 95 | SYS_DRAW_LINE | EBX=x0 ECX=y0 EDX=x1 ESI=y1 EDI=color |
| 96 | SYS_DRAW_TRIANGLE | EBX=x0\|(y0<<16) ECX=x1\|(y1<<16) EDX=x2\|(y2<<16) ESI=color |
| 97 | SYS_BLIT | EBX=src ECX=dst_x EDX=dst_y ESI=w\|(h<<16) EDI=colorkey |
| 98 | SYS_DIRTY_PRESENT | no args — flush dirty shadow rect to LFB |
| 99 | SYS_PSF_LOAD | EBX=filename_ptr -> EAX=0 success / -1 fail |
| 100 | SYS_PSF_CHAR | EBX=x ECX=y EDX=codepoint ESI=fg_color |
| 101 | SYS_PCI_FIND | EBX=vendor_id ECX=device_id -> EAX=bdf or -1 |
| 102 | SYS_GETPPID | -> EAX=parent PID |
| 103 | SYS_FORK | -> EAX=child_pid (parent) / 0 (child) / -1 (error) |
| 104 | SYS_DEFRAG | Defrag HBFS: -> EAX=0/-1 |
| 105 | SYS_TLS_CONNECT | TLS 1.2 stub: EBX=fd -> EAX=0/-1 |
| 106 | SYS_AUDIO_REC_START | Start AC97 recording: EBX=buf ECX=max -> EAX=0/-1 |
| 107 | SYS_AUDIO_REC_READ | Read recorded PCM: EBX=buf ECX=max -> EAX=bytes/-1 |
| 108 | SYS_AUDIO_REC_STOP | Stop recording -> EAX=0 |
| 109 | SYS_AUDIO_OPEN | Open mixer channel: EBX=fmt ECX=rate -> EAX=ch/-1 |
| 110 | SYS_AUDIO_WRITE | Write to mixer: EBX=ch ECX=buf EDX=len -> EAX=bytes |
| 111 | SYS_AUDIO_CLOSE_CHAN | Close mixer channel: EBX=ch -> EAX=0 |
| 112 | SYS_MSGQ_CREATE | Create msg queue: EBX=key ECX=max_msgs -> EAX=qid/-1 |
| 113 | SYS_MSGQ_SEND | Send: EBX=qid ECX=buf EDX=len -> EAX=0/-1 |
| 114 | SYS_MSGQ_RECV | Recv: EBX=qid ECX=buf EDX=max -> EAX=bytes/-1 |
| 115 | SYS_MSGQ_CLOSE | Destroy queue: EBX=qid -> EAX=0/-1 |
| 116 | SYS_DUP | Dup fd:    EBX=fd -> EAX=new_fd/-1 |
| 117 | SYS_DUP2 | Dup2 fd:   EBX=src ECX=dst -> EAX=dst/-1 |
| 118 | SYS_FCNTL | Fcntl:     EBX=fd ECX=cmd EDX=arg -> EAX=result/-1 |
| 119 | SYS_IOCTL | Ioctl:     EBX=fd ECX=req EDX=arg -> EAX=0/-1 |
| 120 | SYS_MMAP | Mmap:      EBX=addr ECX=len EDX=prot -> EAX=addr/-1 |
| 121 | SYS_MUNMAP | Munmap:    EBX=addr ECX=len -> EAX=0/-1 |
| 122 | SYS_MPROTECT | Mprotect:  EBX=addr ECX=len EDX=prot -> EAX=0/-1 |
| 123 | SYS_SELECT | Select:    EBX=nfds ECX=rfds EDX=wfds ESI=efds EDI=tv -> EAX=count |
| 124 | SYS_CLOCK_GETTIME | ClkGetTime:EBX=clk ECX=timespec* -> EAX=0/-1 |
| 125 | SYS_NANOSLEEP | Nanosleep: EBX=rqtp ECX=rmtp -> EAX=0/-1 |
| 126 | SYS_GETTIMEOFDAY | Gtimeofday:EBX=timeval* ECX=tz* -> EAX=0 |
| 127 | SYS_GETUID | Getuid: -> EAX=uid |
| 128 | SYS_SETUID | Setuid:    EBX=uid -> EAX=0/-1 |
| 129 | SYS_GETGID | Getgid: -> EAX=gid |
| 130 | SYS_SETGID | Setgid:    EBX=gid -> EAX=0/-1 |
| 131 | SYS_GETEUID | Geteuid: -> EAX=euid |
| 132 | SYS_GETEGID | Getegid: -> EAX=egid |
| 133 | SYS_ACCESS | Access:    EBX=path ECX=mode -> EAX=0/-1 |
| 134 | SYS_PIPE2 | Pipe2:     EBX=pipefd[2] ECX=flags -> EAX=0/-1 |
| 135 | SYS_SURFACE_CREATE | Create surface: EBX=w ECX=h EDX=x ESI=y -> EAX=surf_id/-1 |
| 136 | SYS_SURFACE_COMMIT | Commit dirty:   EBX=id ECX=dx EDX=dy ESI=dw EDI=dh -> EAX=0/-1 |
| 137 | SYS_SURFACE_DESTROY | Destroy:        EBX=surf_id -> EAX=0/-1 |
| 138 | SYS_SURFACE_MOVE | Move:           EBX=id ECX=x EDX=y -> EAX=0/-1 |
| 139 | SYS_SURFACE_RESIZE | Resize:         EBX=id ECX=w EDX=h -> EAX=0/-1 |
| 140 | SYS_ALARM | alarm():        EBX=seconds (0=cancel) -> EAX=prev_secs |
| 141 | SYS_GETXATTR | Getxattr: EBX=file ECX=key EDX=val_buf ESI=max -> EAX=len/-1 |
| 142 | SYS_SETXATTR | Setxattr: EBX=file ECX=key EDX=val_ptr        -> EAX=0/-1 |
| 143 | SYS_SIGACTION | sigaction(): EBX=signum ECX=handler EDX=old_ptr -> EAX=0/-1 |
| 144 | SYS_SIGRETURN | sigreturn(): (no args) — return from signal handler |
| 145 | SYS_FSTAT | fstat(fd, stat_buf) -> 0/-1 |
| 146 | SYS_FTRUNCATE | ftruncate(fd, size) -> 0/-1 |
| 147 | SYS_FCHMOD | fchmod(fd, mode) -> 0/-1 |
| 148 | SYS_FCHOWN | fchown(fd, uid, gid) -> 0/-1 |
| 149 | SYS_FSYNC | fsync(fd) -> 0/-1 |
| 150 | SYS_LINK | link(old, new) -> 0/-1 |
| 151 | SYS_ISATTY | isatty(fd) -> 1/0 |
| 152 | SYS_SETSID | setsid() -> sid/-1 |
| 153 | SYS_GETSID | getsid(pid) -> sid/-1 |
| 154 | SYS_WAIT | wait(status*) -> pid/-1 |
| 155 | SYS_PAUSE | pause() -> -1 |
| 156 | SYS_UMASK | umask(mask) -> old_mask |
| 157 | SYS_SIGPENDING | sigpending(set*) -> 0 |
| 158 | SYS_SIGSUSPEND | sigsuspend(mask*) -> -1 |
| 159 | SYS_SETITIMER | setitimer(which, new*, old*) -> 0/-1 |
| 160 | SYS_GETITIMER | getitimer(which, cur*) -> 0/-1 |
| 161 | SYS_SETENV | setenv(name, val, over) -> 0/-1 |
| 162 | SYS_UNSETENV | unsetenv(name) -> 0 |
| 163 | SYS_UNAME | uname(utsname*) -> 0/-1 |
| 164 | SYS_UTIME | utime(path, utimbuf*) -> 0/-1 |
| 165 | SYS_SYSCONF | sysconf(name) -> value/-1 |
| 166 | SYS_SETEUID | seteuid(euid) -> 0/-1 |
| 167 | SYS_SETEGID | setegid(egid) -> 0/-1 |
| 168 | SYS_SETREUID | setreuid(ruid, euid) -> 0/-1 |
| 169 | SYS_SETREGID | setregid(rgid, egid) -> 0/-1 |
| 170 | SYS_SENDTO | sendto(fd,buf,len,fl,addr) -> bytes/-1 |
| 171 | SYS_RECVFROM | recvfrom(fd,buf,len,fl,from) -> bytes/-1 |
| 172 | SYS_SETSOCKOPT | setsockopt(fd,lvl,opt,val,len) -> 0/-1 |
| 173 | SYS_GETSOCKOPT | getsockopt(fd,lvl,opt,val*,len*) -> 0/-1 |
| 174 | SYS_GETSOCKNAME | getsockname(fd,addr,len*) -> 0/-1 |
| 175 | SYS_GETPEERNAME | getpeername(fd,addr,len*) -> 0/-1 |
| 176 | SYS_SHUTDOWN | shutdown(fd,how) -> 0/-1 |
| 177 | SYS_TCGETATTR | tcgetattr(fd,termios*) -> 0/-1 |
| 178 | SYS_TCSETATTR | tcsetattr(fd,act,termios*) -> 0/-1 |
| 179 | SYS_TCDRAIN | tcdrain(fd) -> 0/-1 |
| 180 | SYS_TCFLUSH | tcflush(fd,queue) -> 0/-1 |
| 181 | SYS_GETERRNO | geterrno() -> EAX=per-task errno |
