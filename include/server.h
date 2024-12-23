#ifndef SERVER_H
#define SERVER_H

#include <arpa/inet.h>
#include <getopt.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define SA struct sockaddr

#define USAGE_MSG "./bin/zotReg_server [-h] PORT_NUMBER COURSE_FILENAME LOG_FILENAME"\
                  "\n  -h                 Displays this help menu and returns EXIT_SUCCESS."\
                  "\n  PORT_NUMBER        Port number to listen on."\
                  "\n  COURSE_FILENAME    File to read course information from at the start of the server"\
                  "\n  LOG_FILENAME       File to output server actions into. Create/overwrite, if exists\n"
typedef struct node {
    void* data;          // pointer to the data to be stored
    struct node* next;   // pointer to the next node in the linked list
} node_t;

typedef struct list {
    node_t* head;        // pointer to the node_t at the head of the list
    int length;          // number of items in the list
    int (*comparator)(const void*,const  void*);     // function pointer for in order insertion
    void (*printer)(void*, FILE*);  // function pointer for printing the data stored
    void (*deleter)(void*);              // function pointer for deleting any dynamically 
                                         // allocated items within the data stored
} list_t;

// Functions already implemented
list_t* CreateList(int (*compare)(const void*, const void*), void (*print)(void*,FILE*),
                   void (*delete)(void*));
void InsertAtHead(list_t* list, void* val_ref);
void InsertAtTail(list_t* list, void* val_ref);
void InsertInOrder(list_t* list, void* val_ref);
void PrintLinkedList(list_t* list, FILE* fp);

//

typedef struct {
    int clientCnt;  
    int threadCnt;  
    int totalAdds;  
    int totalDrops; 
} stats_t;   

stats_t curStats;  

typedef struct {
    char* username;	
    int socket_fd;	
    pthread_t tid;
    uint32_t enrolled;	
    uint32_t waitlisted;
} user_t;

typedef struct {
    char* title; 
    int   maxCap;      
    list_t enrollment; 
    list_t waitlist;   
} course_t; 

course_t courseArray[32];
//LL functions
list_t* CreateList(int (*compare)(const void*, const void*), void (*print)(void*, FILE*),
                   void (*delete)(void*)) {
    list_t* list = malloc(sizeof(list_t));
    list->comparator = compare;
    list->printer = print;
    list->deleter = delete;
    list->length = 0;
    list->head = NULL;
    return list;
}

void InsertAtHead(list_t* list, void* val_ref) {
    if(list == NULL || val_ref == NULL)
        return;
    if (list->length == 0) list->head = NULL;

    node_t** head = &(list->head);
    node_t* new_node;
    new_node = malloc(sizeof(node_t));

    new_node->data = val_ref;

    new_node->next = *head;

    // moves list head to the new node
    *head = new_node;
    list->length++;
}

void InsertAtTail(list_t* list, void* val_ref) {
    if (list == NULL || val_ref == NULL)
        return;
    if (list->length == 0) {
        InsertAtHead(list, val_ref);
        return;
    }

    node_t* head = list->head;
    node_t* current = head;
    while (current->next != NULL) {
        current = current->next;
    }

    current->next = malloc(sizeof(node_t));
    current->next->data = val_ref;
    current->next->next = NULL;
    list->length++;
}


void InsertInOrder(list_t* list, void* val_ref) {
    if(list == NULL || val_ref == NULL)
        return;
    if (list->length == 0) {
        InsertAtHead(list, val_ref);
        return;
    }

    node_t** head = &(list->head);
    node_t* new_node;
    new_node = malloc(sizeof(node_t));
    new_node->data = val_ref;
    new_node->next = NULL;

    if (list->comparator(new_node->data, (*head)->data) < 0) {
        new_node->next = *head;
        *head = new_node;
    } else if ((*head)->next == NULL) {
        (*head)->next = new_node;
    } else {
        node_t* prev = *head;
        node_t* current = prev->next;
        while (current != NULL) {
            if (list->comparator(new_node->data, current->data) > 0) {
                if (current->next != NULL) {
                    prev = current;
                    current = current->next;
                } else {
                    current->next = new_node;
                    break;
                }
            } else {
                prev->next = new_node;
                new_node->next = current;
                break;
            }
        }
    }
    list->length++;
}

void PrintLinkedList(list_t* list, FILE* fp) {
    if(list == NULL)
        return;

    node_t* head = list->head;
    while (head != NULL) {
        list->printer(head->data, fp);
        fprintf(fp, "\n");
        head = head->next;
    }
}

// INSERT FUNCTIONS HERE
int run_server(int port_number, const char* coursesFileName, const char* logFileName);
int init_server(int port_number);
void init_curStats();
int init_Courses(const char* coursesFileName);
void init_sigHandler();
void DestroyLinkedList(list_t** list);
void userDeleter(void * data);
int userComparator(const void* data1, const void* data2);
void userPrinter(void* data, FILE* fp);
void PrintCourses();
int sendErrno(int client_fd, uint8_t msg);
void PrintUser();
void PrintCurStats();
user_t* FindUser(const char* buffer);
void PrintUser();
void PrintCurStats();
void PrintEnrollment(int index);
void PrintWaitlist(int index);
int respondCList(int client_fd);
char* buildCLIST();
int respondSched(int client_fd, user_t* user);
void enrollmentPrinter(void* data, FILE* fp);
void waitlistPrinter(void* data, FILE* fp);
void removeFromEnrollment(int index, const char* username);
user_t* popWaitlistHead(int courseToAdd);
char * buildSched(user_t* user);
#endif

