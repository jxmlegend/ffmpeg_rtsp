#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include "cJSON.h"

#define MYPORT 10081
#define MAX_DATA 512//Socket接收到的数据最大程度 

#define ERR_EXIT(m) \
    do { \
        perror(m); \
        exit(EXIT_FAILURE); \
    } while (0)

bool gBoolWakeup = false;

void parseJson(cJSON* rootJson)
{
    if(NULL == rootJson){
        printf("rootJson == NULL");
	    return;
    }
	
	cJSON *json_tmp = cJSON_GetObjectItem(rootJson, "topic");
	if(NULL == json_tmp || cJSON_NULL == json_tmp->type)
	{
	    printf("No [topic] item in json");
		return;
	}
	
	char* topic_str = json_tmp->valuestring;
	printf("topic_str: %s\n", topic_str);
	
	cJSON* data_json = cJSON_GetObjectItem(rootJson, "data");
	
	json_tmp = cJSON_GetObjectItem(data_json, "DeviceName");
	if(NULL == json_tmp || cJSON_NULL == json_tmp->type)
	{
	    printf("No [DeviceName] item in json");
		return;
	}
	char* deviceName_str = json_tmp->valuestring;
	printf("deviceName_str: %s\n", deviceName_str);
	
	json_tmp = cJSON_GetObjectItem(data_json, "ProductKey");
	if(NULL == json_tmp || cJSON_NULL == json_tmp->type)
	{
	    printf("No [ProductKey] item in json");
		return;
	}
	char* productKey_str = json_tmp->valuestring;
	printf("productKey_str: %s\n", productKey_str);

    char topic_item[50] = {0};
	char *ptr = strrchr(topic_str, '/');
	if(NULL == ptr)
	{
		printf("topic content in json is invalid, topic_str: %s", topic_str);
	}
	else
	{
		sprintf(topic_item, "%s", ptr + 1);
	}
		
	if(!strcmp(topic_item, "LowPowerState"))
    {
        printf("topic is [LowPowerState]\n");
		json_tmp = cJSON_GetObjectItem(data_json, "State");
		if(NULL == json_tmp || cJSON_NULL == json_tmp->type){
			printf("No [State] item in json");
			return;
		}
		int state_int = json_tmp->valueint;
		printf("state_int: %d\n", state_int);
    } else if (!strcmp(topic_item, "Alarm")){
        printf("topic is [Alarm]\n");
		json_tmp = cJSON_GetObjectItem(data_json, "AlarmDesc");
		if(NULL == json_tmp || cJSON_NULL == json_tmp->type){
			printf("No [AlarmDesc] item in json");
			return;
		}
		char* alarmDesc_str = json_tmp->valuestring;
		printf("alarmDesc_str: %d\n", alarmDesc_str);
		
		json_tmp = cJSON_GetObjectItem(data_json, "TimeStamp");
		if(NULL == json_tmp || cJSON_NULL == json_tmp->type){
			printf("No [TimeStamp] item in json");
			return;
		}
		char* timeStamp_str = json_tmp->valuestring;
		printf("timeStamp_str: %d\n", timeStamp_str);
		
		json_tmp = cJSON_GetObjectItem(data_json, "AlarmSeq");
		if(NULL == json_tmp || cJSON_NULL == json_tmp->type) {
			printf("No [AlarmSeq] item in json");
			return;
		}
		int alarmSeq_int = json_tmp->valueint;
		printf("alarmSeq_int: %d\n", alarmSeq_int);
    } else if (!strcmp(topic_item, "Wakeup")) {
        printf("topic is [Wakeup]\n");
		json_tmp = cJSON_GetObjectItem(data_json, "State");
		if(NULL == json_tmp || cJSON_NULL == json_tmp->type){
			printf("No [State] item in json");
			return;
		}
		int state_int = json_tmp->valueint;
		printf("state_int: %d\n", state_int);
		if(state_int) {
		    gBoolWakeup = true;
		} else {
			gBoolWakeup = false;
		}
    } else {
		printf("invalid topic_item: %s", topic_item);
	}

	printf("\n");
}

void echo_ser(int sock)
{
    char recvbuf[MAX_DATA] = {0};
    struct sockaddr_in peeraddr;
    socklen_t peerlen;
	cJSON* root;
    int n;

    while (1)
    {
        peerlen = sizeof(peeraddr);
        memset(recvbuf, 0, sizeof(recvbuf));
        n = recvfrom(sock, recvbuf, sizeof(recvbuf), 0, (struct sockaddr *)&peeraddr, &peerlen);
        if (n <= 0)
        {
            if (errno == EINTR)
                continue;
            
            ERR_EXIT("recvfrom error");
        }
        else if(n > 0)
        {
            printf("接收到的数据：%s\n", recvbuf);
            root = cJSON_Parse(recvbuf);
			parseJson(root);
        }
    }
    close(sock);
}
 
int main(void)
{
    int sock;
    if ((sock = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
        ERR_EXIT("socket error");
    
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(MYPORT);
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    printf("监听%d端口\n", MYPORT);
    if (bind(sock, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0){
	    ERR_EXIT("bind error");
	}
    
    echo_ser(sock);
    
    return 0;
}

