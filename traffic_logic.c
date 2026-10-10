/*
 * traffic_logic.c
 * All backend logic
 */

#include "traffic.h"

// Global definitions
int waitingCycles[MAX] = {0, 0, 0, 0};
int currentTime = 0;
int vehicleID   = 1;

// =====================================================
//                    QUEUE FUNCTIONS
// =====================================================

void initializeQueue(Queue *q)
{
    q->front = NULL;
    q->rear  = NULL;
    q->size  = 0;
}

int isEmpty(Queue *q)
{
    return q->front == NULL;
}

void enqueue(Queue *q, Vehicle v)
{
    node *newNode = (node *)malloc(sizeof(node));
    if (!newNode) return;

    newNode->vehicle = v;
    newNode->next    = NULL;

    if (q->front == NULL) {
        q->front = newNode;
        q->rear  = newNode;
    } else {
        q->rear->next = newNode;
        q->rear       = newNode;
    }
    q->size++;
}

int dequeue(Queue *q, Vehicle *v)
{
    if (q->front == NULL) return 0;

    node *temp = q->front;
    *v         = temp->vehicle;
    q->front   = q->front->next;

    if (q->front == NULL) q->rear = NULL;

    q->size--;
    free(temp);
    return 1;
}

// =====================================================
//                 DIRECTION FUNCTIONS
// =====================================================

char *directionName(int direction)
{
    if (direction == NORTH) return "NORTH";
    if (direction == SOUTH) return "SOUTH";
    if (direction == EAST)  return "EAST";
    return "WEST";
}

Queue *getQueue(Queue *north, Queue *south, Queue *east, Queue *west, int direction)
{
    if (direction == NORTH) return north;
    if (direction == SOUTH) return south;
    if (direction == EAST)  return east;
    return west;
}

// =====================================================
//                  FILE LOG
// =====================================================

void logTraffic(char message[])
{
    FILE *fp = fopen("traffic_log.txt", "a");
    if (!fp) return;
    fprintf(fp, "[Time %d] %s\n", currentTime, message);
    fclose(fp);
}

// =====================================================
//                 WAITING TIME
// =====================================================

int waitingTime(Queue *q)
{
    if (q->front == NULL) return 0;
    return currentTime - q->front->vehicle.arrivalTime;
}

// =====================================================
//              COUNT EMERGENCY VEHICLES
// =====================================================

int countEmergency(Queue *q)
{
    node *temp = q->front;
    int   count = 0;
    while (temp) {
        if (temp->vehicle.emergency) count++;
        temp = temp->next;
    }
    return count;
}

// =====================================================
//                  PRIORITY
// =====================================================

int calculatePriority(Queue *q, int direction)
{
    if (q->front == NULL) return 0;

    int priority = 0;
    priority += q->size * 10;
    priority += waitingTime(q);

    int emergencyCount = countEmergency(q);
    if (emergencyCount > 0) {
        priority += 1000;
        priority += emergencyCount * 100;
    }
    priority += waitingCycles[direction] * 20;
    return priority;
}

// =====================================================
//                  HEAP FUNCTIONS
// =====================================================

void swap(Priority *a, Priority *b)
{
    Priority temp = *a;
    *a = *b;
    *b = temp;
}

void heapifyUp(Heap *h, int index)
{
    if (index == 0) return;
    int parent = (index - 1) / 2;
    if (h->arr[index].priority > h->arr[parent].priority) {
        swap(&h->arr[index], &h->arr[parent]);
        heapifyUp(h, parent);
    }
}

void insertHeap(Heap *h, int direction, int priority)
{
    if (h->size == MAX) return;
    h->arr[h->size].direction = direction;
    h->arr[h->size].priority  = priority;
    heapifyUp(h, h->size);
    h->size++;
}

void heapifyDown(Heap *h, int index)
{
    int left    = 2 * index + 1;
    int right   = 2 * index + 2;
    int largest = index;

    if (left < h->size && h->arr[left].priority > h->arr[largest].priority)
        largest = left;
    if (right < h->size && h->arr[right].priority > h->arr[largest].priority)
        largest = right;

    if (largest != index) {
        swap(&h->arr[index], &h->arr[largest]);
        heapifyDown(h, largest);
    }
}

Priority deleteHeap(Heap *h)
{
    Priority result = {-1, 0};
    if (h->size == 0) return result;

    result = h->arr[0];
    h->size--;
    if (h->size > 0) {
        h->arr[0] = h->arr[h->size];
        heapifyDown(h, 0);
    }
    return result;
}

void buildHeap(Heap *h, Queue *north, Queue *south, Queue *east, Queue *west)
{
    h->size = 0;
    for (int d = 0; d < 4; d++) {
        Queue *q  = getQueue(north, south, east, west, d);
        int    pr = calculatePriority(q, d);
        if (pr > 0) insertHeap(h, d, pr);
    }
}

// =====================================================
//              GREEN SIGNAL TIME
// =====================================================

int greenTime(Queue *q)
{
    int t = 5 + q->size;
    if (t > 20) t = 20;
    return t;
}

// =====================================================
//              UPDATE STARVATION
// =====================================================

void updateStarvation(int selected)
{
    for (int i = 0; i < 4; i++) {
        if (i == selected) waitingCycles[i] = 0;
        else               waitingCycles[i]++;
    }
}

// =====================================================
//               PASS VEHICLES
// =====================================================

void passVehicles(Queue *q, int direction, int green, Statistics *stats)
{
    Vehicle v;
    int number = green / 2;
    if (number > 5) number = 5;

    for (int i = 0; i < number; i++) {
        if (!dequeue(q, &v)) break;

        int wait = currentTime - v.arrivalTime;
        if (wait < 0) wait = 0;

        stats->vehiclesPassed++;
        stats->totalWaitingTime += wait;
        if (wait > stats->maxWaitingTime) stats->maxWaitingTime = wait;

        if      (direction == NORTH) stats->passedNorth++;
        else if (direction == SOUTH) stats->passedSouth++;
        else if (direction == EAST)  stats->passedEast++;
        else                         stats->passedWest++;

        char message[100];
        sprintf(message, "Vehicle %d passed from %s", v.id, directionName(direction));
        logTraffic(message);
    }
    currentTime += green;
}

// =====================================================
//             FIND EMERGENCY DIRECTION
// =====================================================

int findEmergency(Queue *north, Queue *south, Queue *east, Queue *west)
{
    for (int d = 0; d < 4; d++) {
        Queue *q = getQueue(north, south, east, west, d);
        if (countEmergency(q) > 0) return d;
    }
    return -1;
}

// =====================================================
//              RUN SIMULATION (returns selected dir)
// =====================================================

void runSimulation(Queue *north, Queue *south, Queue *east, Queue *west, Statistics *stats)
{
    Heap     heap;
    Priority selected;
    int      direction;
    Queue   *q;

    int emergencyDirection = findEmergency(north, south, east, west);

    if (emergencyDirection != -1) {
        direction = emergencyDirection;
    } else {
        buildHeap(&heap, north, south, east, west);
        if (heap.size == 0) {
            currentTime += 5;
            return;
        }
        selected  = deleteHeap(&heap);
        direction = selected.direction;
    }

    q = getQueue(north, south, east, west, direction);
    int green = greenTime(q);
    passVehicles(q, direction, green, stats);
    updateStarvation(direction);
}

// =====================================================
//                SAVE REPORT
// =====================================================

void saveReport(Statistics *stats)
{
    FILE *fp = fopen("simulation_report.txt", "w");
    if (!fp) return;

    float average = stats->vehiclesPassed
        ? (float)stats->totalWaitingTime / stats->vehiclesPassed
        : 0.0f;

    fprintf(fp, "SMART TRAFFIC FLOW SIMULATOR\n\n");
    fprintf(fp, "Total Vehicles     : %d\n", stats->totalVehicles);
    fprintf(fp, "Vehicles Passed    : %d\n", stats->vehiclesPassed);
    fprintf(fp, "Emergency Vehicles : %d\n", stats->emergencyVehicles);
    fprintf(fp, "Average Waiting    : %.2f\n", average);
    fprintf(fp, "Maximum Waiting    : %d\n",   stats->maxWaitingTime);
    fprintf(fp, "\nNorth Passed : %d\n", stats->passedNorth);
    fprintf(fp, "South Passed : %d\n",   stats->passedSouth);
    fprintf(fp, "East Passed  : %d\n",   stats->passedEast);
    fprintf(fp, "West Passed  : %d\n",   stats->passedWest);
    fclose(fp);
}

// =====================================================
//                 RESET
// =====================================================

void clearQueue(Queue *q)
{
    Vehicle v;
    while (!isEmpty(q)) dequeue(q, &v);
}

void resetSimulation(Queue *north, Queue *south, Queue *east, Queue *west, Statistics *stats)
{
    clearQueue(north);
    clearQueue(south);
    clearQueue(east);
    clearQueue(west);

    stats->totalVehicles    = 0;
    stats->vehiclesPassed   = 0;
    stats->emergencyVehicles = 0;
    stats->passedNorth      = 0;
    stats->passedSouth      = 0;
    stats->passedEast       = 0;
    stats->passedWest       = 0;
    stats->totalWaitingTime = 0;
    stats->maxWaitingTime   = 0;

    currentTime = 0;
    vehicleID   = 1;
    for (int i = 0; i < 4; i++) waitingCycles[i] = 0;
}
