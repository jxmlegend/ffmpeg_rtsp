#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>   //调用 sleep() 函数
#include <pthread.h>  //调用 pthread_create() 函数

void *ThreadFun(void *arg)
{
    if (arg == NULL) {
        printf("arg is NULL\n");
    }
    else {
        printf("%s\n", (char*)arg);
    }
	
	for(int i = 0; i < 36; i++){
		printf("i: %d\n", i);
		sleep(1);
	}

	//if(system("pkill ffplay") == -1){
	//if(system("killall ffplay -e") == -1){
	if(system("killall ffplay") == -1){
		printf("execl filed!\n");
		perror("becasue");
	}
	
    return NULL;
}

int main()
{
    int res;
    char * url = "http://c.biancheng.net";
    pthread_t myThread;
    res = pthread_create(&myThread, NULL, ThreadFun, NULL);
    if (res != 0) {
        printf("线程创建失败");
        return 0;
    }
	
	//if(system("ffplay /home/limd/Music/Paradise.mp3 -loglevel quiet") == -1){
	//if(system("ffplay rtsp://wowzaec2demo.streamlock.net/vod/mp4:BigBuckBunny_115k.mp4 -loglevel quiet") == -1){
	//	printf("execl filed!\n");
	//	perror("becasue");
	//}
	
	for(int i = 0; i < 37; i++){
		printf("i: %d\n", i);
		sleep(1);
	}

    printf("--------------------> main\n");
	
    return 0;
}