MyFind – Protocol Topics
1. Introduction
- Purpose of the MyFind project.
- Searching for multiple filenames in a directory.
- Main program functionality.
2. Parallelization
- Using fork() to create child processes.
- One child process for each filename.
- Difference between Parent Process and Child Process.
- Parallel execution of file searches.
3. Synchronization of stdout
- Problem of multiple processes writing to stdout simultaneously.
- Using a shared lock file.
- Synchronization with fcntl().
- Writing complete output lines using write().
- Releasing the lock with F_UNLCK.
4. Process Management
- Using waitpid() in the parent process.
- Waiting for all child processes to terminate.
- Preventing zombie processes.
5. Conclusion 