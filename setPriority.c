#include "types.h"
#include "stat.h"
#include "user.h"

int main(int argc, char* argv[])
{
    if(argc != 3)
    {
        printf(1,"wrong number of arguments.\n");
        exit();
    }

    int newpriority = atoi(argv[1]);
    if(newpriority<0 || newpriority>100)
    {
        printf(1,"priority can only be between 0 to 100\n");
        exit();
    }
    int pid = atoi(argv[2]);

    int oldpriority = set_priority(newpriority,pid);
    if(oldpriority == -1)
    {
        printf(1,"process doesnt exist\n");
    }
    exit();
}