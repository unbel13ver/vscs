#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/poll.h>

#define DEFPORT 12345 //Стандартный порт
#define CLSLOTS 1024 //Количество пользователей
#define BUFFER 256 //Длина служебной части сообщения
#define MSGLEN 65535 //Макс. длина сообщения

struct pollfd fds[CLSLOTS];
struct sockaddr_in srv_addr;

int i;
int countfds=0;

void accept_new_client(int);
void client_action(int);

char username[CLSLOTS][BUFFER] = {{0,0}};
char *u_name[CLSLOTS];
int name_flag[CLSLOTS] = {0};


int main(int argc, char **argv)
{
	int poll_result = 0;
	int srv_socket = 0;
	unsigned short portnum 
	= DEFPORT;

	bzero(&name_flag, CLSLOTS); //Создание массива имён пользователей
	for (i = 0; i < CLSLOTS; i++)
	{
		u_name[i] = username[i];
	}

	printf("Starting Simple Chat Server...\n"); //Получение номера порта, на котором должен работать сервер
	if (argc > 1)
	{
		if (sscanf(argv[1], "%hd", &portnum)!=1)
		{
			portnum = DEFPORT;
			printf("Default port used (12345).\n");
		} else {
			printf("Port № %d used.\n", portnum);
		}
	} else {
		printf("Default port used (12345).\n");
	}

	//Создание сокета
	srv_socket = socket(AF_INET, SOCK_STREAM, 0);
	if (srv_socket < 0)
	{
		perror("socket creation failed");
		exit(EXIT_FAILURE);
	}

	//Привязка сокета 
	srv_addr.sin_family = AF_INET;
	srv_addr.sin_port = htons(portnum);
	srv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bzero(&srv_addr.sin_zero, sizeof(srv_addr.sin_zero)); 

	if (bind(srv_socket, (struct sockaddr*) &srv_addr, sizeof(srv_addr)))
	{
		perror("bind() failed");
		exit(EXIT_FAILURE);
	}

	//Перевод сокета в прослушивающий режим
	if (listen(srv_socket, SOMAXCONN))
	{
		perror("listen() failed");
		exit(EXIT_FAILURE);
	} else printf("Server successfully established.\n");

	//Заполнение структуры fds
	fds[0].fd = srv_socket;	//Источник события
	fds[0].events = POLLIN;	//Ожидаемое событие
	fds[0].revents = 0;		//Возвращаемое событие
	countfds++;		//Счетчик структур

	while(1)
	{
		poll_result = poll(fds, countfds, -1); //Ожидание события
		if (poll_result < 0) //Результат "poll == 0" не проверяется, т.к. он происходит по... 
		{
			perror("poll() failed"); //...истечению таймера, кот. не используется
			exit(EXIT_FAILURE);
		}
		for (i = 0; i < countfds; i++) //Проверка события
		{
			if (fds[i].revents&POLLIN)
			{
				if (fds[i].fd == srv_socket) //Проверка источника события.
				{
					accept_new_client(srv_socket); //Если источник - серверный сокет
				} else {
					client_action(i); //Если другой источник
				}
			}
		}
	}
}

//Процедура "Принятие нового клиента"
void accept_new_client(int s_sock)
{
	int cln_socket;
	char greetings[BUFFER] = "Welcome to our chatroom!!!\nYour 1st message will be your name!:)\n";
	struct sockaddr_in cln_addr;
	socklen_t len;

	len = sizeof(cln_addr);
	cln_socket = accept(s_sock,(struct sockaddr*) &cln_addr, &len); //Приём нового соединения
	if (cln_socket < 0)
	{
		perror("accept() failed");
		exit(EXIT_FAILURE);
	}

	fds[countfds].fd = cln_socket; //Источник события - клиентский сокет
	fds[countfds].events = POLLIN;
	fds[countfds].revents = 0;

	printf("new user accepted. Total: %d user(s)\n", countfds);
	if (fcntl(cln_socket, F_SETFL, O_NONBLOCK))
	{
		perror("failed to set non-blocking mode");
	}
	write(cln_socket, greetings, BUFFER);
	name_flag[countfds] = 1;  //массив флагов наличия имён для пользователей. 1 == Нет имени.
	countfds++;
}

//Процедура "Взаимодействие клиентов"
void client_action(int client_id)
{
	int rc;
	int disc_flag = 0;
	int i;
	int len;
	char msg[MSGLEN];
	char newmsg[MSGLEN+BUFFER];

	bzero(&newmsg, sizeof(newmsg));
	bzero(&msg, sizeof(msg));

	rc = recv(fds[client_id].fd, msg, MSGLEN, 0);
	if (rc < 0)
	{
		perror("recv() failed");
		exit(EXIT_FAILURE);
	}

	if (rc == 0) //Отключение клиента
	{
		disc_flag = 1; //Флаг отключения. 1 == Отключение произошло.
		bzero(&msg, sizeof(msg));
		if (name_flag[client_id] == 0) //Если у пользователя уже было имя
		{
			sprintf(newmsg, "Server: User %s left our chatroom\n", username[client_id]);
		} else {
			bzero(&newmsg, sizeof(newmsg));
			close(fds[client_id].fd); //Если имени не было
			goto __exit;
		}
		close(fds[client_id].fd);
	}

	if (rc > 0)
	{
		sprintf(newmsg, "%s: %s", username[client_id], msg); //Создание сообщения для рассылки
		if (name_flag[client_id] == 1) //Если  у клиента ещё нет имени
		{
			if (msg[strlen(msg)-2] == '\r') //Проверка telnet/NetCat
			{
				msg[strlen(msg)-2] = '\0';
			} else {
				msg[strlen(msg)-1] = '\0';
			}
			sprintf(u_name[client_id], "%s", msg);
			sprintf(newmsg, "Server: We have a new user - %s\n", username[client_id]); //Замена собщения для рассылки
			name_flag[client_id] = 0;
		}
	}
	
	len = sizeof(newmsg); //Рассылка сообщения всем, кроме написавшего
	for (i = 1; i < client_id; i++)
	{
		rc = send(fds[i].fd, newmsg, len, 0);
		if (rc < 0)
		{
			perror("left send() failed");
			exit(EXIT_FAILURE);
		}
	}
	for (i = client_id+1; i < countfds; i++)
	{
		rc = send(fds[i].fd, newmsg, len, 0);
		if (rc < 0)
		{
			perror("right send() failed");
			exit(EXIT_FAILURE);
		}
	}

__exit:
	if (disc_flag == 1)
	{
		for (i = client_id; i < countfds; i++)
		{
			fds[i].fd = fds[i+1].fd; //Сдвиг структур после отключения пользователя.
			sprintf(u_name[i], "%s", u_name[i+1]); //Сдвиг имён
		}
		countfds--;
		printf("user disconnected. Total: %d user(s)\n", countfds-1);
	}
}
