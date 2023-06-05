#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

/* 先唤醒，再设置 */
 
#define POSTURL    "https://iotapi.dev.hongyanyun.cn/controlscreen/device/setproperty"  //用于测试阶段  //接口已废弃 2022.10.24
//#define POSTURL    "https://iotapi.hongyanyun.cn"  //用于正式阶段
//#define POSTFIELDS "{\"productKey\":\"a164msJWX5c\",\"deviceName\":\"H000059200400029\",\"items\":\"{\\\"LocalEventIP\\\":\\\"\\\"}\"}"     //clear IP
#define POSTFIELDS "{\"productKey\":\"a164msJWX5c\",\"deviceName\":\"H000059200400029\",\"items\":\"{\\\"LocalEventIP\\\":\\\"192.168.124.12\\\"}\"}"  //H3C
//#define POSTFIELDS "{\"productKey\":\"a164msJWX5c\",\"deviceName\":\"H000059200400029\",\"items\":\"{\\\"LocalEventIP\\\":\\\"192.168.2.23\\\"}\"}"  //Only for test

struct memory {
   char *response;
   size_t size;
};
struct memory chunk = {0};

static size_t cb(void *data, size_t size, size_t nmemb, void *userp)
{
   size_t realsize = size * nmemb;
   struct memory *mem = (struct memory *)userp;
 
   char *ptr = realloc(mem->response, mem->size + realsize + 1);
   if(ptr == NULL)
   {
	   printf("realloc fail\n");
       return 0;  /* out of memory! */
   }
 
   mem->response = ptr;
   memcpy(&(mem->response[mem->size]), data, realsize);
   mem->size += realsize;
   mem->response[mem->size] = 0;
   printf("~~~~~~~~~~~~~~~~~~~~~~mem->response: %s", mem->response);
   free(ptr);
 
   return realsize;
}

int main(int argc,char *argv[])
{
	CURL *curl;
	CURLcode res;
	//char path[] = "save_file.txt";
    //FILE *file = fopen(path, "w");
	//char str[1000] = "";
 
	struct curl_slist *http_header = NULL;
 
	curl = curl_easy_init();
	if (!curl)
	{
		fprintf(stderr,"curl init failed\n");
		return -1;
	}
 
    printf("POSTURL:%s\n", POSTURL);
	printf("POSTFIELDS:%s\n", POSTFIELDS);
	
	curl_easy_setopt(curl, CURLOPT_URL, POSTURL); //url地址
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0); //不检查ssl，可访问https
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);  //不检查ssl，可访问https
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS,POSTFIELDS); //post参数
	curl_easy_setopt(curl, CURLOPT_POST, 1); //设置问非0表示本次操作为post
	curl_easy_setopt(curl, CURLOPT_VERBOSE, 1); //打印调试信息

	//http_header = curl_slist_append(NULL, "Content-Type:application/json;charset=UTF-8");
	http_header = curl_slist_append(NULL, "Content-Type:application/json");
	curl_slist_append(http_header, "debug:1");  //用于测试阶段
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, http_header);

	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1); //设置为非0,响应头信息location
	curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10);//接收数据时超时设置，如果10秒内数据未

 
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cb);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
  
	res = curl_easy_perform(curl);
 
	if (res != CURLE_OK)
	{
		switch(res)
		{
			case CURLE_UNSUPPORTED_PROTOCOL:
				fprintf(stderr,"-----------> 不支持的协议,由URL的头部指定\n");
			case CURLE_COULDNT_CONNECT:
				fprintf(stderr,"-----------> 不能连接到remote主机或者代理\n");
			case CURLE_HTTP_RETURNED_ERROR:
				fprintf(stderr,"-----------> http返回错误\n");
			case CURLE_READ_ERROR:
				fprintf(stderr,"-----------> 读本地文件错误\n");
			default:
				fprintf(stderr,"-----------> 返回值:%d\n",res);
		}
		return -1;
	}
	else
	{
		printf("Success -----------> 返回值:%d\n",res);
	}

    //fclose(file);
	curl_easy_cleanup(curl);
    //printf("end  limd -----------> \n");
    return 0;
}
