#include <stdio.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#define Concurrency 20
#define ITERATION 500
#define maxline (1024 * 2 * 1024)
#define readbuff (65536 * 10)
//在disk上的文件
char *filepathDN[19] = {"/usr/file1.txt", "/usr/file2.txt", "/usr/file3.txt", "/usr/file4.txt", "/usr/file5.txt", "/usr/file6.txt", "/usr/file7.txt", "/usr/file8.txt", "/usr/file9.txt", "/usr/file10.txt", "/usr/file11.txt", "/usr/file12.txt", "/usr/file13.txt", "/usr/file14.txt", "/usr/file15.txt", "/usr/file16.txt", "/usr/file17.txt", "/usr/file18.txt", "/usr/file19.txt"};
//在ram上的文件
char *filepathRN[19] = {"/root/myram/file1.txt", "/root/myram/file2.txt", "/root/myram/file3.txt", "/root/myram/file4.txt", "/root/myram/file5.txt", "/root/myram/file6.txt", "/root/myram/file7.txt", "/root/myram/file8.txt", "/root/myram/file9.txt", "/root/myram/file10.txt", "/root/myram/file11.txt", "/root/myram/file12.txt", "/root/myram/file13.txt", "/root/myram/file14.txt", "/root/myram/file15.txt", "/root/myram/file16.txt", "/root/myram/file17.txt", "/root/myram/file18.txt", "/root/myram/file19.txt"};
char buffer[maxline] = "ABCDEFGhigklmnOPQRSTuvwxyz"; //用于写的缓冲区

/*写文件:打开文件，判断返回值，如果正常打开文件就判断是否随机写，进行写操作*/
void write_file(int blocksize, bool isrand, char *filepath)
{
    int fs = open(filepath, O_CREAT | O_RDWR | O_SYNC, 0755);
    if (fs <= 0)
    {
        exit(1);
    }
    for (int i = 0; i < ITERATION;)
    {
        int w = write(fs, buffer, blocksize);
        if (w < 0) //说明无法写入，可能空间不够
        {
            perror("write error!");
            exit(1);
        }
        int a = rand() % (50 * 1024 * 1024);
        if (isrand)
        {
            if (lseek(fs, a, SEEK_SET) == -1)
                printf("lseek error");
        }
        if (w == blocksize)
        {
            i++;
        }
    }
    lseek(fs, 0, SEEK_SET);
    close(fs);
}

/*读文件:打开文件，判断返回值，如果正常打开就判断是否随机读，进行读操作*/
void read_file(int blocksize, bool isrand, char *filepath)
{
    int fs = open(filepath, O_CREAT | O_RDWR | O_SYNC, 0755);
    if (fs <= 0)
    {
        exit(1);
    }
    char buff[readbuff];
    for (int i = 0; i < ITERATION;)
    {
        int r = read(fs, buff, blocksize);
        if (r <= 0)
        {
            printf("read error!\n");
            exit(1);
        }
        if (isrand)
        {
            int a = rand() % (50 * 1024 * 1024);
            if (lseek(fs, a, SEEK_SET) == -1)
                printf("lseek error\n");
        }
        if (r == blocksize)
        {
            i++;
        }
    }
    lseek(fs, 0, SEEK_SET);
    close(fs);
}

//计算时间差，在读或写操作前后分别取系统时间，然后计算差值即为时间差。
long long get_time_left(struct timespec start, struct timespec end)
{
    return ((end.tv_sec - start.tv_sec) * 1000000000 + (end.tv_nsec - start.tv_nsec)); /* ns */
}

/*主函数：首先创建和命名文件，通过循环执行read_file和write_file函数测试读写差异。
测试blocksize和concurrency对测试读写速度的影响，最后输出结果。*/
int main()
{
    srand((unsigned)time(NULL));
    int block, i;
    int FORKTIME;
    for (i = 0; i < maxline;i += 10)
    {
        strncat(buffer, "abcdefghij", 10);
    }
    struct timespec start, end;
    /*等待子进程完成后，获取计算时间，计算读写操作所花时间，延时，吞吐量等*/
    for (block = 64; block <= 65536;)
    {
        for (FORKTIME = 1; FORKTIME <= Concurrency;)
        {
            clock_gettime(CLOCK_REALTIME, &start);
            for (i = 0; i <= FORKTIME; i++)
            {
                if (fork() == 0)
                {
                    //随机写
                    //write_file(block, true, filepathDN[i]);
                    // write_file(block, true, filepathRN[i]);
                    //顺序写
                    write_file(block, false, filepathDN[i]);
                    // write_file(block, false, filepathRN[i]);
                    // close(filepathRN[i]);
                    //随机读
                    //read_file(block,true,filepathDN[i]);
                    // read_file(block,true,filepathRN[i]);
                    
                    //顺序读
                    // read_file(block,false,filepathDN[i]);
                    // read_file(block,false,filepathRN[i]);
                    exit(1);
                }
            }
            //等待所有子进程结束
            while (wait(NULL) != -1); //等待所有子进程结束
            clock_gettime(CLOCK_REALTIME, &end);
            double alltime = get_time_left(start, end) / 1000.0;                     // us
            //一共是fork个进程，每一个进程都要进行iteration个操作，所以进行一个读操作或者是写操作时间即延时为
            //latency
            double latency = (alltime / 1000) / (double)ITERATION / FORKTIME;          //毫秒       
            //再由latency/1000化成秒得到timeuse        
            double timeuse = latency / 1000;           //秒
            double block_kB = (double)block / 1024.0; /*由MB变为kB */
            double throughput = block_kB / timeuse / 1024.0;
            double iops=FORKTIME*ITERATION/timeuse;
            printf("block %d,forktime %d,ops %f,iops %f\n", block, FORKTIME, throughput,iops);                                             
            FORKTIME += 3;
        }
        block *= 4; // block依次为64B，256B，1KB，4KB，16KB，64KB
    }
    return 0;
}
