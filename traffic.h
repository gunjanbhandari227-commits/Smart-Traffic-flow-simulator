#ifndef TRAFFIC_H
#define TRAFFIC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX 4

#define NORTH 0
#define SOUTH 1
#define EAST  2
#define WEST  3

// ---------------- VEHICLE ----------------
typedef struct {
    int  id;
    char type[20];
    int  arrivalTime;
    int  emergency;
} Vehicle;

// ---------------- LINKED LIST NODE ----------------
typedef struct node {
    Vehicle      vehicle;
    struct node *next;
} node;

// ---------------- QUEUE ----------------
typedef struct {
    node *front;
    node *rear;
    int   size;
} Queue;

// ---------------- PRIORITY NODE ----------------
typedef struct {
    int direction;
    int priority;
} Priority;

// ---------------- MAX HEAP ----------------
typedef struct {
    Priority arr[MAX];
    int      size;
} Heap;

// ---------------- STATISTICS ----------------
typedef struct {
    int totalVehicles;
    int vehiclesPassed;
    int emergencyVehicles;

    int passedNorth;
    int passedSouth;
    int passedEast;
    int passedWest;

    int totalWaitingTime;
    int maxWaitingTime;
} Statistics;

// ---------------- GLOBALS ----------------
extern int waitingCycles[MAX];
extern int currentTime;
extern int vehicleID;

// ---------------- PROTOTYPES ----------------
void      initializeQueue(Queue *q);
int       isEmpty(Queue *q);
void      enqueue(Queue *q, Vehicle v);
int       dequeue(Queue *q, Vehicle *v);
char     *directionName(int direction);
Queue    *getQueue(Queue *north, Queue *south, Queue *east, Queue *west, int direction);
void      logTraffic(char message[]);
int       waitingTime(Queue *q);
int       countEmergency(Queue *q);
int       calculatePriority(Queue *q, int direction);
void      swap(Priority *a, Priority *b);
void      heapifyUp(Heap *h, int index);
void      insertHeap(Heap *h, int direction, int priority);
void      heapifyDown(Heap *h, int index);
Priority  deleteHeap(Heap *h);
void      buildHeap(Heap *h, Queue *north, Queue *south, Queue *east, Queue *west);
int       greenTime(Queue *q);
void      updateStarvation(int selected);
void      passVehicles(Queue *q, int direction, int green, Statistics *stats);
int       findEmergency(Queue *north, Queue *south, Queue *east, Queue *west);
void      runSimulation(Queue *north, Queue *south, Queue *east, Queue *west, Statistics *stats);
void      saveReport(Statistics *stats);
void      clearQueue(Queue *q);
void      resetSimulation(Queue *north, Queue *south, Queue *east, Queue *west, Statistics *stats);

#endif /* TRAFFIC_H */
