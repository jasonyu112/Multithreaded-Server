#include "server.h"
#include "protocol.h"
#include <pthread.h>
#include <signal.h>

const char exit_str[] = "LOGOUT";

char buffer[BUFFER_SIZE];
pthread_mutex_t userListLock;
pthread_mutex_t logFileLock; 
pthread_mutex_t curStatsLock; 
pthread_mutex_t courseArrayLock;

int listen_fd;
FILE* logFile = NULL;
int courses_initialized = 0;
list_t* userList = NULL;


void sigint_handler(int sig)
{
    //printf("shutting down server\n");
    PrintCourses();
    PrintUser();
    PrintCurStats();
    fclose(logFile);
    close(listen_fd);
    exit(0);
}

int sendErrno(int client_fd, uint8_t msg){
    petrV_header* response = calloc(sizeof(petrV_header),1);
    response->msg_len = 0;
    response->msg_type = msg;
    int wr_errno = wr_msg(client_fd, response, "");
    free(response);
    return wr_errno;
}

void *process_client(void* clientfd_ptr){
    int client_fd = *(int *)clientfd_ptr;
    free(clientfd_ptr);
    int received_size;
    fd_set read_fds;

    int retval;

    petrV_header petr;
    user_t* user = malloc(sizeof(user_t));
    user->username = NULL;
    user->socket_fd = client_fd;
    user->waitlisted = 0;
    user->enrolled = 0;
    petr.msg_len = 0;
    petr.msg_type = 0;
    int check_login = 1;

    while(1){
        FD_ZERO(&read_fds);
        FD_SET(client_fd, &read_fds);
        retval = select(client_fd + 1, &read_fds, NULL, NULL, NULL);
        if (retval!=1 && !FD_ISSET(client_fd, &read_fds)){
            printf("Error with select() function\n");
            break;
        }
        
        rd_msgheader(client_fd, &petr);
        char * buffer = calloc(petr.msg_len,1);
        if(petr.msg_len != 0){
            received_size = read(client_fd, buffer, petr.msg_len);
        }
        if(received_size < 0){
            printf("Receiving failed\n");
            break;
        }else if(received_size == 0){
            continue;
        }

        if(check_login){
            if(petr.msg_type!= LOGIN){
                free(buffer);
                free(user);
                break;
            }
            else{
                //char* username = malloc(petr.msg_len);
                //strcpy(username,buffer);
                if((FindUser(buffer)) == NULL){
                    //new user
                    pthread_mutex_lock(&curStatsLock);
                    curStats.clientCnt++;
                    curStats.threadCnt++;
                    pthread_mutex_unlock(&curStatsLock);
                    char * username = malloc(petr.msg_len);
                    strcpy(username, buffer);
                    user->username = username;
                    pthread_mutex_lock(&userListLock);
                    InsertInOrder(userList, user);
                    pthread_mutex_unlock(&userListLock);
                    int wr_errno = sendErrno(client_fd, OK);
                    check_login = 0;
                    pthread_mutex_lock(&logFileLock);
                    fprintf(logFile, "CONNECTED %s\n", user->username);
                    pthread_mutex_unlock(&logFileLock);
                }
                else{
                    //existing user
                    free(user);
                    pthread_mutex_lock(&userListLock);
                    user = FindUser(buffer);
                    pthread_mutex_unlock(&userListLock);
                    if(user->socket_fd!=-1){
                        //error
                        sendErrno(client_fd, EUSRLGDIN);
                        break;
                    }else{
                        //continue
                        pthread_mutex_lock(&curStatsLock);
                        curStats.clientCnt++;
                        curStats.threadCnt++;
                        pthread_mutex_unlock(&curStatsLock);
                        pthread_mutex_lock(&userListLock);
                        user->socket_fd = client_fd;
                        pthread_mutex_unlock(&userListLock);
                        int wr_errno = sendErrno(client_fd, OK);
                        sendErrno(client_fd, OK);
                        check_login = 0;
                        pthread_mutex_lock(&logFileLock);
                        fprintf(logFile, "RECONNECTED %s\n", user->username);
                        pthread_mutex_unlock(&logFileLock);
                    }
                }
            }
        }
        
        if(petr.msg_type == ENROLL){
            int courseToAdd = atoi(buffer);
            if(courseToAdd >= courses_initialized){
                int wr_errno = sendErrno(client_fd, ECNOTFOUND);
                pthread_mutex_lock(&curStatsLock);
                curStats.totalAdds++;
                pthread_mutex_unlock(&curStatsLock);
                pthread_mutex_lock(&logFileLock);
                fprintf(logFile, "%s NOTFOUND_E %d\n", user->username,courseToAdd);
                pthread_mutex_unlock(&logFileLock);
            }
            else if(courseArray[courseToAdd].enrollment.length==courseArray[courseToAdd].maxCap){
                int wr_errno = sendErrno(client_fd, ECDENIED);
                pthread_mutex_lock(&curStatsLock);
                curStats.totalAdds++;
                pthread_mutex_unlock(&curStatsLock);
                pthread_mutex_lock(&logFileLock);
                fprintf(logFile, "%s NOENROLL %d\n", user->username,courseToAdd);
                pthread_mutex_unlock(&logFileLock);
            }
            else if(user->enrolled & (1<<courseToAdd)){
                int wr_errno = sendErrno(client_fd, ECDENIED);
                pthread_mutex_lock(&curStatsLock);
                curStats.totalAdds++;
                pthread_mutex_unlock(&curStatsLock);
                pthread_mutex_lock(&logFileLock);
                fprintf(logFile, "%s NOENROLL %d\n", user->username,courseToAdd);
                pthread_mutex_unlock(&logFileLock);
            }
            else if(courseArray[courseToAdd].enrollment.length<courseArray[courseToAdd].maxCap){
                //int wr_errno = sendErrno(client_fd, ECDENIED);
                int wr_errno = sendErrno(client_fd, OK);
                pthread_mutex_lock(&curStatsLock);
                curStats.totalAdds++;
                pthread_mutex_unlock(&curStatsLock);
                pthread_mutex_lock(&courseArrayLock);
                InsertInOrder(&courseArray[courseToAdd].enrollment, user);
                pthread_mutex_unlock(&courseArrayLock);
                pthread_mutex_lock(&userListLock);
                user->enrolled |= (1<<courseToAdd);
                pthread_mutex_unlock(&userListLock);
                pthread_mutex_lock(&logFileLock);
                fprintf(logFile, "%s ENROLL %d %d\n", user->username,courseToAdd, user->enrolled);
                pthread_mutex_unlock(&logFileLock);
            }
        }
        
        if(petr.msg_type == CLIST){
            int wr_errno = respondCList(client_fd);
            pthread_mutex_lock(&logFileLock);
            fprintf(logFile, "%s CLIST\n", user->username);
            pthread_mutex_unlock(&logFileLock);
        }
        if(petr.msg_type == SCHED){
            if(user->enrolled == 0 && user->waitlisted == 0){
                int wr_errno = sendErrno(client_fd, ENOCOURSES);
                pthread_mutex_lock(&logFileLock);
                fprintf(logFile, "%s NOSCHED\n", user->username);
                pthread_mutex_unlock(&logFileLock);
            }
            else{
                pthread_mutex_lock(&courseArrayLock);
                int wr_errno = respondSched(client_fd, user);
                pthread_mutex_unlock(&courseArrayLock);
                pthread_mutex_lock(&logFileLock);
                fprintf(logFile, "%s SCHED\n", user->username);
                pthread_mutex_unlock(&logFileLock);
            }
        }

        if(petr.msg_type == DROP){
            int courseToAdd = atoi(buffer);
            if(courseToAdd >= courses_initialized){
                int wr_errno = sendErrno(client_fd, ECNOTFOUND);
                pthread_mutex_lock(&logFileLock);
                fprintf(logFile, "%s NOTFOUND_D %d\n", user->username,courseToAdd);
                pthread_mutex_unlock(&logFileLock);
            }
            else if(!(user->enrolled & (1<<courseToAdd))){
                int wr_errno = sendErrno(client_fd, ECDENIED);
                pthread_mutex_lock(&logFileLock);
                fprintf(logFile, "%s NODROP %d\n", user->username,courseToAdd);
                pthread_mutex_unlock(&logFileLock);
            }
            else{
                int wr_errno = sendErrno(client_fd, OK);
                pthread_mutex_lock(&courseArrayLock);
                removeFromEnrollment(courseToAdd, user->username);
                pthread_mutex_unlock(&courseArrayLock);
                pthread_mutex_lock(&curStatsLock);
                curStats.totalDrops++;
                pthread_mutex_unlock(&curStatsLock);
                pthread_mutex_lock(&userListLock);
                user->enrolled &= ~(1<<courseToAdd);
                pthread_mutex_unlock(&userListLock);
                pthread_mutex_lock(&logFileLock);
                fprintf(logFile, "%s DROP %d %d\n", user->username,courseToAdd, user->enrolled);
                pthread_mutex_unlock(&logFileLock);
                if(courseArray[courseToAdd].waitlist.length != 0){
                    pthread_mutex_lock(&courseArrayLock);
                    user_t * temp_user = popWaitlistHead(courseToAdd);
                    InsertInOrder(&courseArray[courseToAdd].enrollment, temp_user);
                    pthread_mutex_unlock(&courseArrayLock);
                    pthread_mutex_lock(&userListLock);
                    temp_user->waitlisted &= ~(1<<courseToAdd);
                    temp_user->enrolled |= (1<<courseToAdd);
                    pthread_mutex_unlock(&userListLock);
                    pthread_mutex_lock(&curStatsLock);
                    curStats.totalAdds++;
                    pthread_mutex_unlock(&curStatsLock);
                    pthread_mutex_lock(&logFileLock);
                    fprintf(logFile, "%s WAITADD %d %d\n", temp_user->username,courseToAdd, temp_user->enrolled);
                    pthread_mutex_unlock(&logFileLock);
                }
            }
        }

        if(petr.msg_type == WAIT){
            int courseToAdd = atoi(buffer);
            if(courseToAdd >= courses_initialized){
                int wr_errno = sendErrno(client_fd, ECNOTFOUND);
                pthread_mutex_lock(&logFileLock);
                fprintf(logFile, "%s NOTFOUND_W %d\n", user->username,courseToAdd);
                pthread_mutex_unlock(&logFileLock);
            }
            else if(courseArray[courseToAdd].enrollment.length < courseArray[courseToAdd].maxCap){
                int wr_errno = sendErrno(client_fd, ECDENIED);
                pthread_mutex_lock(&logFileLock);
                fprintf(logFile, "%s NOWAIT %d\n", user->username,courseToAdd);
                pthread_mutex_unlock(&logFileLock);
            }
            else if(user->enrolled & (1<<courseToAdd)){
                int wr_errno = sendErrno(client_fd, ECDENIED);
                pthread_mutex_lock(&logFileLock);
                fprintf(logFile, "%s NOWAIT %d\n", user->username,courseToAdd);
                pthread_mutex_unlock(&logFileLock);
            }
            else{
                int wr_errno = sendErrno(client_fd, OK);
                pthread_mutex_lock(&userListLock);
                user->waitlisted |= (1<<courseToAdd);
                pthread_mutex_unlock(&userListLock);
                pthread_mutex_lock(&courseArrayLock);
                InsertAtTail(&courseArray[courseToAdd].waitlist, user);
                pthread_mutex_unlock(&courseArrayLock);
                pthread_mutex_lock(&logFileLock);
                fprintf(logFile, "%s WAIT %d %d\n", user->username,courseToAdd, user->waitlisted);
                pthread_mutex_unlock(&logFileLock);
            }
        }

        if(petr.msg_type == LOGOUT){
            free(buffer);
            pthread_mutex_lock(&userListLock);
            user->socket_fd = -1;
            int wr_errno = sendErrno(client_fd, OK);
            pthread_mutex_unlock(&userListLock);
            pthread_mutex_lock(&logFileLock);
            fprintf(logFile, "%s LOGOUT\n", user->username);
            pthread_mutex_unlock(&logFileLock);
            break;
        }
        free(buffer);
    }
 
    //printf("Close current client connection\n");
    close(client_fd);
    return NULL;
}

user_t* popWaitlistHead(int courseToAdd){
    list_t* list = &courseArray[courseToAdd].waitlist;
    node_t* head = list->head;
    user_t* head_user = (user_t*)head->data;
    list->head = head->next;
    list->length--;
    return head_user;
}

void removeFromEnrollment(int index, const char* username){
    list_t* list = &courseArray[index].enrollment;
    node_t* head = list->head;
    node_t* tail = head->next;
    user_t* checkHead = (user_t*)head->data;
    if(strcmp(checkHead->username,username) == 0){
        list->head = tail;
        list->length--;
        return;
    }
    while(tail != NULL){
        user_t* data = (user_t*)tail->data;
        if(strcmp(checkHead->username,username) == 0){
            head->next = tail->next;
        }
        head = head->next;
        tail = tail->next;
    }
    list->length--;
}

int respondSched(int client_fd, user_t* user){
    petrV_header* response = calloc(sizeof(petrV_header),1);
    response->msg_type = SCHED;
    char* courses = buildSched(user);
    response->msg_len = strlen(courses)+1;
    int wr_errno = wr_msg(client_fd, response, courses);
    free(response);
    free(courses);
    return wr_errno;
}

char * buildSched(user_t* user){
    char* buffer = calloc(2024,1);
    char * username = user->username;
    for(int x = 0; x<courses_initialized; x++){
        list_t* list1 = &courseArray[x].enrollment;
        list_t* list2 = &courseArray[x].waitlist;
        int enrolledBit = (user->enrolled & (1<<x));
        int waitBit = (user->waitlisted & (1<<x));
        printf("course number %d: enrolled bit %d\n", x, enrolledBit);
        printf("course number %d: waitlist bit %d\n", x, waitBit);
        if(enrolledBit){
            char * defaultStart = "Course ";
            char courseIndex[2];
            sprintf(courseIndex, "%d", x);
            strcat(buffer, defaultStart);
            strcat(buffer, courseIndex);
            strcat(buffer, " - ");
            strcat(buffer, courseArray[x].title);
            strcat(buffer, "\n");
        }
        else if (waitBit){
            char * defaultStart = "Course ";
            char courseIndex[2];
            sprintf(courseIndex, "%d", x);
            strcat(buffer, defaultStart);
            strcat(buffer, courseIndex);
            strcat(buffer, " - ");
            strcat(buffer, courseArray[x].title);
            strcat(buffer, " (WAITING)\n");
        }
    }
    return buffer;
}

int respondCList(int client_fd){
    petrV_header* response = calloc(sizeof(petrV_header),1);
    response->msg_type = CLIST;
    char* courses = buildCLIST();
    response->msg_len = strlen(courses)+1;
    int wr_errno = wr_msg(client_fd, response, courses);
    free(response);
    free(courses);
    return wr_errno;
}

char* buildCLIST(){
    char * buffer = calloc(2048, 1);
    for(int x = 0; x<courses_initialized; x++){
        char * defaultStart = "Course ";
        char courseIndex[2];
        sprintf(courseIndex, "%d", x);
        strcat(buffer, defaultStart);
        strcat(buffer, courseIndex);
        strcat(buffer, " - ");
        strcat(buffer, courseArray[x].title);
        strcat(buffer, "\n");
    }
    return buffer;
}

user_t* FindUser(const char* buffer){
    node_t* head = userList->head;
    while(head != NULL){
        user_t* data = (user_t*)head->data;
        if(strcmp(buffer, data->username) == 0){
            return data;
        }
        head = head->next;
    }
    return NULL;
}
void init_sigHandler(){
    struct sigaction myaction = {{0}};
    myaction.sa_handler = sigint_handler;
    if(sigaction(SIGINT, &myaction, NULL) == -1){
        printf("signal handler failed to install\n");
    }
}

void PrintCourses(){
    for(int x = 0; x<courses_initialized; x++){
        printf("%s, %d, %d, ",courseArray[x].title, courseArray[x].maxCap, courseArray[x].enrollment.length);
        PrintEnrollment(x);
        printf(", ");
        PrintWaitlist(x);
        printf("\n");
    }
}

int init_Courses(const char* coursesFileName){
    if(coursesFileName == NULL){
        return 2;
    }
    FILE * coursesFile = fopen(coursesFileName, "r");
    if(coursesFile == NULL){
        return 2;
    }   
    //read line for line
    char * line = NULL;
    size_t len = 0;
    ssize_t read;
    int coursesIndex = 0;

    while((read = getline(&line, &len, coursesFile))!=-1){
        if (coursesIndex >=32){
           break; 
        }
        char* course = malloc(strlen(line)+1);
        strcpy(course, line);

        *strchr(course, ';') = '\0';
        int maxSeating = atoi(strchr(course, '\0')+1);
        courseArray[coursesIndex].title = course;
        courseArray[coursesIndex].maxCap = maxSeating;
        courseArray[coursesIndex].enrollment.printer = &enrollmentPrinter;
        courseArray[coursesIndex].waitlist.printer = &waitlistPrinter;
        coursesIndex++;
    }
    printf("Server initialized with %d courses.\n", coursesIndex);
    courses_initialized = coursesIndex;

    fclose(coursesFile);
    return 0;
}

void init_curStats()
{
    curStats.clientCnt = 0;
    curStats.threadCnt = 0;
    curStats.totalAdds = 0;
    curStats.totalDrops = 0;
}
int userComparator(const void* data1, const void* data2){
    char* word1 = ((user_t*)data1)->username;
    char* word2 = ((user_t*)data2)->username;
    for(word1, word2; (*word1)!='\0'||(*word2)!='\0'; word1++, word2++){
        if(*word1 < *word2){
            return -1;
        }
        else if(*word1>*word2){
            return 1;
        }
    }
    if(strlen(word1)<strlen(word2)){
        return -1;
    }
    if(strlen(word1)>strlen(word2)){
        return 1;
    }
    return 0;
}
void userPrinter(void* data, FILE* fp){
    user_t* d = (user_t*)data;
    fprintf(fp, "%s, %d, %d\n", d->username, d->enrolled, d->waitlisted);
}

int run_server(int server_port, const char* coursesFileName, const char* logFileName){
    int courseErrno = init_Courses(coursesFileName);
    if(courseErrno == 2){
        return 2;
    }
    listen_fd = init_server(server_port); // Initiate server and start listening on specified port
    int client_fd;
    logFile = fopen(logFileName, "w");
    init_curStats();
    userList = CreateList(&userComparator, &userPrinter, &userDeleter);

    init_sigHandler();
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);

    pthread_t tid;
    
    while(1){
        // Wait and Accept the connection from client
        //printf("Wait for new client connection\n");
        int* client_fd = malloc(sizeof(int));
        *client_fd = accept(listen_fd, (SA*)&client_addr, (socklen_t*)&client_addr_len);
        if (*client_fd < 0) {
            //printf("server acccept failed\n");
            exit(EXIT_FAILURE);
        }
        else{
            //printf("Client connetion accepted\n");
            pthread_create(&tid, NULL, process_client, (void *)client_fd); 
        }
    }
    
    bzero(buffer, BUFFER_SIZE);
    close(listen_fd);
    fclose(logFile);
    //DestroyLinkedList(&usersList);
    return 0;
}

int init_server(int server_port){
    int sockfd;
    struct sockaddr_in servaddr;

    // socket create and verification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        //printf("socket creation failed...\n");
        exit(EXIT_FAILURE);
    }
    else
        //printf("Socket successfully created\n");

    bzero(&servaddr, sizeof(servaddr));

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(server_port);

    int opt = 1;
    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (char *)&opt, sizeof(opt))<0)
    {
	perror("setsockopt");exit(EXIT_FAILURE);
    }

    // Binding newly created socket to given IP and verification
    if ((bind(sockfd, (SA*)&servaddr, sizeof(servaddr))) != 0) {
        //printf("socket bind failed\n");
        exit(EXIT_FAILURE);
    }
    else
        //printf("Socket successfully binded\n");

    // Now server is ready to listen and verification
    if ((listen(sockfd, 1)) != 0) {
        printf("Listen failed\n");
        exit(EXIT_FAILURE);
    }
    else
        printf("Currently listening on port %d.\n", server_port);

    return sockfd;
}

int main(int argc, char *argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "h")) != -1) {
        switch (opt) {
            case 'h':
                fprintf(stderr, USAGE_MSG);
                exit(EXIT_FAILURE);
        }
    }

    // 3 positional arguments necessary
    if (argc != 4) {
        fprintf(stderr, USAGE_MSG);
        exit(EXIT_FAILURE);
    }
    unsigned int port_number = atoi(argv[1]);
    char * poll_filename = argv[2];
    char * log_filename = argv[3];

    //INSERT CODE HERE
    return run_server(port_number, poll_filename, log_filename);
    
}

void DestroyLinkedList(list_t** list) {
    if(list == NULL){
		return;
	}
	if(*list == NULL){
		return;
	}
	node_t * iter = (*list) -> head;
	node_t * prev = (*list) -> head;
	while(iter!=NULL){
		iter = iter->next;
        userDeleter(prev->data);
		free(prev->data);
		free(prev);
		prev = iter;
	}
	free(*list);
}

void userDeleter(void * data){
    return;
}

void PrintUser(){
    node_t* head = userList->head;
    while(head != NULL){
        userList->printer(head->data, stderr);
        head = head->next;
    }
}
void enrollmentPrinter(void* data, FILE* fp){
    user_t* d = (user_t*)data;
    fprintf(fp, "%s", d->username);
    
}
void waitlistPrinter(void* data, FILE* fp){
    user_t* d = (user_t*)data;
    fprintf(fp, "%s", d->username);
}
void PrintEnrollment(int index){
    node_t* head = courseArray[index].enrollment.head;
    while(head != NULL){
        courseArray[index].enrollment.printer(head->data, stdout);
        head = head->next;
        if(head!=NULL){
            fprintf(stdout, ";");
        }
    }
}
void PrintWaitlist(int index){
    node_t* head = courseArray[index].waitlist.head;
    while(head != NULL){
        courseArray[index].waitlist.printer(head->data, stdout);
        head = head->next;
        if(head!=NULL){
            fprintf(stdout, ";");
        }
    }
}

void PrintCurStats(){
    fprintf(stderr,"%d, %d, %d, %d\n", curStats.clientCnt, curStats.threadCnt,curStats.totalAdds,curStats.totalDrops);
}