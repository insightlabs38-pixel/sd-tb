// rt: run a command, print wall seconds, max RSS KB, user and sys seconds.
#include <stdio.h>
#include <stdlib.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
int main(int c, char **v) {
  struct timespec a, b; clock_gettime(CLOCK_MONOTONIC, &a);
  pid_t p = fork();
  if (!p) { execvp(v[1], v + 1); _exit(127); }
  int st; struct rusage r; wait4(p, &st, 0, &r);
  clock_gettime(CLOCK_MONOTONIC, &b);
  fprintf(stderr, "wall %.4f rss %ld user %.3f sys %.3f rc %d\n", (b.tv_sec - a.tv_sec) + (b.tv_nsec - a.tv_nsec) / 1e9, r.ru_maxrss,
          r.ru_utime.tv_sec + r.ru_utime.tv_usec / 1e6, r.ru_stime.tv_sec + r.ru_stime.tv_usec / 1e6, WEXITSTATUS(st));
  return 0;
}
