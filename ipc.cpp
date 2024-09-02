/*
		MIT License
  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#include "ipc.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/types.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "cJSON.h"

#define MAX_MSG_NUM (64)

#define LOCAL_URL	"127.0.0.1"


typedef struct msg_callback_s
{
	int msgId;
	msg_callback cb;
}msg_callback_t;

typedef struct ipc_msg
{
	int msgId;
	int resultCode;
	void *param;
}IPC_MSG;


static int bRun = 1;

static msg_callback_t s_msgCbAry[MAX_MSG_NUM] = {0};

static int ipc_socket = -1;

static struct sockaddr_in remote_addr;


static void distribute_msg(char *msg)
{
	cJSON *root = cJSON_Parse(msg);
	if (root)
	{
		cJSON *idItem = cJSON_GetObjectItem(root, "id");
		cJSON *msgItem = cJSON_GetObjectItem(root, "msg");
		if (idItem)
		{
			int msgId = idItem->valueint;
			
			char *body = nullptr;
			if (msgItem && msgItem->type == cJSON_String)
				body = msgItem->valuestring;
			
			for (int i = 0; i < MAX_MSG_NUM; i++)
			{
				if (0 < s_msgCbAry[i].msgId && msgId == s_msgCbAry[i].msgId)
				{
					s_msgCbAry[i].cb((void *)body);
					break;
				}
			}
		}
		
		cJSON_Delete(root);
	}

	
}


static void *ipc_msg_process(void *param)
{
	socklen_t len = sizeof(struct sockaddr_in);
	int ret;
	char buffer[32786] = {0};
	while (bRun) {
		memset(buffer, 0, 32786);
		ret = recvfrom(ipc_socket, buffer, sizeof(buffer), 0, (struct sockaddr *)&remote_addr, &len);
		if (ret > 0) {
			buffer[ret] = 0;
			distribute_msg(buffer);
		}
	}
	
	return nullptr;
}


int IPC_init(int pid_port)
{
	
	struct sockaddr_in ipc_addr;
	
	if(pid_port <= 0 || pid_port >= 65535)
	{
		fprintf(stderr, "port is invalid.\n");
		return -1;
	}
	
	
	ipc_socket = socket(AF_INET, SOCK_DGRAM, 0);

	if(0 > ipc_socket) {
		fprintf (stderr, "IPC socket create error!\n");
		return -1;
	}

	memset(&ipc_addr, 0, sizeof(ipc_addr));
	ipc_addr.sin_family = AF_INET;
	ipc_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	ipc_addr.sin_port = htons (pid_port);
	
	
	if(bind(ipc_socket, (struct sockaddr *)&ipc_addr, sizeof(struct sockaddr)) < 0) {
		fprintf (stderr, "IPC sokcet bind error!\n");
		close(ipc_socket);
		ipc_socket = -1;
		return -1;
	}
	bRun = 1;
	pthread_t pid;
	int ret = pthread_create(&pid, NULL, ipc_msg_process, NULL);
	
	if (ret){
		fprintf(stderr, "ipc pthread_create\n");
		close(ipc_socket);
		ipc_socket = -1;
		return -1;
	}
	
	pthread_detach(pid);
	return 0;
	
}


void IPC_deinit(void)
{
	bRun = 0;
	if(ipc_socket > 0)
	{
		close(ipc_socket);
		ipc_socket = -1;
	}
	
}

int IPC_snd(int pid_port, int msgId, const char *msg, char *ackMsg, int timeout)
{
	if ( 0 >= pid_port || pid_port >= 65536)
	{
		return -1;
	}
	size_t len = 0;
	struct sockaddr_in to_addr;
	struct timeval tv;
	fd_set rfds;
	socklen_t s_len = sizeof(to_addr);
	int fd = -1, res = -1, ret = -1;
	char buffer[32786] = {0};
	FD_ZERO(&rfds);
	

	fd = socket(AF_INET, SOCK_DGRAM, 0);
	if(fd < 0) {
		
		return -1;
	}

	bzero(&to_addr, sizeof(to_addr));
	to_addr.sin_family = AF_INET;
	to_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	to_addr.sin_port = htons(pid_port);
	
	cJSON *root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "id", msgId);
	if (msg)
		cJSON_AddStringToObject(root, "msg", msg);
	
	char *str = cJSON_PrintUnformatted(root);
	if (str)
	{
		sendto(fd, str, strlen(str), 0, (struct sockaddr *)&to_addr, sizeof (struct sockaddr));
		cJSON_free(str);
	}
	cJSON_Delete(root);
	root = nullptr;



	FD_SET(fd, &rfds);
	if(timeout > 0)
	{
		tv.tv_sec = timeout/1000;
		tv.tv_usec = (timeout%1000)*1000;
	}else{
		tv.tv_sec = 0;
		tv.tv_usec = 700000;		
	}


	select(fd+1, &rfds, NULL, NULL, &tv);
	if (FD_ISSET (fd, &rfds)) {
		
		memset(buffer, 0, 32786);
		ret = recvfrom(fd, buffer, 32786, 0, (struct sockaddr *)&to_addr, &s_len);
		if (ret > 0) {
			root = cJSON_Parse(buffer);
			if (root)
			{
				cJSON *resItem = cJSON_GetObjectItem(root, "result");
				cJSON *msgItem = cJSON_GetObjectItem(root, "msg");
				if (resItem)
				{
					res = resItem->valueint;
				}
				if (msgItem && msgItem->type == cJSON_String && ackMsg)
				{
					memcpy(ackMsg, msgItem->valuestring, strlen(msgItem->valuestring));
				}
				cJSON_Delete(root);
				
				
			}

			
		}else{
			fprintf(stderr, "IPC recvfrom timeout error.\n");
		}
	}
	close (fd);
	fd = -1;
	
	
	return res;
	
}

int IPC_ack(int msgId, int resultCode,const char *param)
{

	cJSON *root = cJSON_CreateObject();
	cJSON_AddNumberToObject(root, "id", msgId);
	cJSON_AddNumberToObject(root, "result", resultCode);
	int ret = -1;
	if (param)
		cJSON_AddStringToObject(root, "msg", param);
	
	char *str = cJSON_PrintUnformatted(root);
	if (str)
	{
		ret = sendto(ipc_socket, str, strlen(str), 0, (struct sockaddr *)&remote_addr, sizeof (struct sockaddr));
		cJSON_free(str);
	}
	cJSON_Delete(root);
	
	return ret;

		
}


int IPC_register_msg(int msgid, msg_callback cb)
{
	for (int i = 0; i < MAX_MSG_NUM; i++)
	{
		if (0 == s_msgCbAry[i].msgId)
		{
			s_msgCbAry[i].cb = cb;
			s_msgCbAry[i].msgId = msgid;
			return 0;
		}
	}
	return -1;
}


