#include "types.h"
#include "stat.h"
#include "user.h"
 
 
int main(int argc, char* argv[])
{
  int pid;
 
  

  pid = fork();

  if(pid > 0)
  {
    int wtime,rtime;
    int status = waitx(&wtime, &rtime);
    printf(1,"Wait time = %d   Run Time = %d  with Status = %d\n",wtime,rtime,status);
    exit();
    
  }
  else if(pid == 0)
  {
    exec(argv[1],argv+1);
    exit();
  }


}