#include <stdio.h>
#include <stdlib.h>

#define OK  0
#define NG -1

//Node Structure
struct node 
{
    int value;
    struct node* nextAddress;
};

//Global head pointer
struct node* listHead = NULL;

//Function declarations
void printList(void);
int insertNodeAtBegin(int value);
int insertNodeAtEnd(int value);
int deleteNodeIfDataIs(int data);
void reverseALinkedList(void);  

void printList(void)
{
    struct node* temp = listHead;
    
    printf(" Linked List: ");
    
    while(temp != NULL)
    {
        printf("  | %d -> ", temp->value);
        temp = temp->nextAddress;
    }
    printf(" NULL\n");
}

int insertNodeAtBegin(int value)
{
    struct node* newNodeToAdd = (struct node*)malloc(sizeof(struct node*));
    
    if(newNodeToAdd == NULL) return NG;
    
    newNodeToAdd->value = value;
    newNodeToAdd->nextAddress = listHead;
    listHead = newNodeToAdd;
    
    return OK;
}

int insertNodeAtEnd(int value)
{
    struct node* newNodeToAdd = (struct node*)malloc(sizeof(struct node*));
    
    if(newNodeToAdd == NULL) return NG;
    
    newNodeToAdd->value = value;
    newNodeToAdd->nextAddress = NULL;
    
    //If the linked List is empty then directly assign the listHead as newNode 
    if(listHead == NULL)
    {
        listHead = newNodeToAdd;
        return OK;
    }
    
    //As the list is not empty, traverse to the last node and assign the newNode address to that last node nextAddress
    struct node* temp = listHead; //TO traverse over the linked list make a copy of the list
    while(temp->nextAddress != NULL){
        temp = temp->nextAddress;
    }
    
    temp->nextAddress = newNodeToAdd;
    
    return OK;
}

int deleteNodeIfDataIs(int value)
{
    struct node* temp = listHead;
    struct node* prev = NULL;
    
    while(temp != NULL) {
        if(temp->value == value) {
            if(prev == NULL) {
                listHead = temp->nextAddress;
            } else {
                prev->nextAddress = temp->nextAddress;
            }
        }
        
        prev = temp;
        temp = temp->nextAddress;
    }
    
    return NG;
}

void reverseALinkedList(void)
{
    struct node *prev = NULL;
    struct node *curr = listHead;
    struct node *next = NULL;

    while (curr != NULL)
    {
        next = curr->nextAddress;   // store next
        curr->nextAddress = prev;   // reverse link
        prev = curr;         // move prev
        curr = next;         // move curr
    }

    listHead = prev; // update head
}

void main()
{
    printList();
    
    insertNodeAtBegin(1);
    insertNodeAtBegin(2);
    
    printList();
    
    insertNodeAtBegin(3);
    
    printList();
    
    insertNodeAtEnd(4);
    printList();
    
    deleteNodeIfDataIs(2);
    printList();
    
    reverseALinkedList();
    
    printList();
    
}
