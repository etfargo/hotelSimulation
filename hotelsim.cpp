/**
 * @file hotelsim.cpp
 * @author Elliot Thompson
 * @brief Project to demonstrate the use of threads and semaphores by simulating hotel activities.
 * Code is based off of our in class examples, samples from Blackboard,
 * information from the sites below, and my own hands.
 * @version 1.0
 * @date 2021-11-19
 * 
 * @copyright Copyright (c) 2021
 * 
 */

/**
 * @brief helper websites
 *  allcaps: https://www.programiz.com/cpp-programming/library-function/cctype/toupper
 *  string vector: https://www.geeksforgeeks.org/array-strings-c-3-different-ways-create/
 *  sem init: https://pubs.opengroup.org/onlinepubs/7908799/xsh/sem_init.html
 *  pass struct to function: https://stackoverflow.com/questions/15181765/passing-structs-to-functions
 *  str_compare: https://www.cplusplus.com/reference/string/string/compare/
 */

#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <semaphore.h>
#include <cctype>
#include <unistd.h>
#include <string>
#include <vector>
#define NUM_GUEST 5

using namespace std;

// define guest data
struct guest_data {
    int id;
    int money;
    string activity;
    int roomKey;
};

// ---- Function Declaration section ----
// guest thread and related functions
void* guest(void* thread_info);
void setSharedGuestInfo_In(guest_data guest);
void setSharedGuestInfo_Out(guest_data guest);
void waitEnterHotel(int guest_id);
void guestExecuteHotelActivity(int guest_id, string activity);
void postExitHotel(int guest_id);
// checkin thread and related functions
void* checkin(void* thread_info);
void greetGuest(int guest_id);
void checkAndAssignRoom();
// chcekout thread and related functions
void* checkout(void* thread_info);
void checkGuestOut();
// process thread and helper functions
string getRandomActivity();
void initializeAllSemaphores();
void printHotelResults();

// ---- semaphore declaration ----
sem_t rooms; // 3, max rooms 
sem_t talkCheckin; // 1, mutual exlusion talking to reservationist
sem_t ordercheckin; // 0, event ordering - guest first
sem_t roomAvail; // 0, event ordering - checkin first??
sem_t hotelActivity; // 0, occurs after checkin
sem_t talkCheckout; // 1, mut. ex. talk wit clerk
sem_t orderCheckout; // 0, event order - guest first
sem_t roomDesignation; // 1, mut. ex. to avoid concurrent clerk modification
sem_t getBalance; // 0 clerk first, then guest
sem_t getPayment; // 0 clerk request, guest pays on checkout
sem_t canExit; // 0, checkout says guest can exit after payment recieved

string activities[4] = {"Pool", "Restaurant", "Fitness center", "Business center"};
int activityCounter[4] = {0, 0, 0, 0}; //P, R, FC, BC
int data[NUM_GUEST]; // warning safe index for guests
struct guest_data guest_data_arr[NUM_GUEST];
struct guest_data shared_guest_in; // passing info guest <-> checkin
struct guest_data shared_guest_out; // passing info guest <-> checkout
vector<int> roomAvailability {0, 0 ,0}; // 0 is open, 1 is full

int main(int argc, char* argv[]){
    int rcG, rcCi, rcCo; // thread return codes for error checking
    
    initializeAllSemaphores();
   
    pthread_t checkinClerk;
    pthread_t checkoutClerk;
    pthread_t guests[NUM_GUEST];
    // create checkin thread
    rcCi = pthread_create(&checkinClerk, NULL, checkin, (void*)1);
    if(rcCi){
        printf("Error! rc checkin is %d\n", rcCi);
            exit(-1);
    }
    // create checkout thread
    rcCo = pthread_create(&checkoutClerk, NULL, checkout, (void*)1);
    if(rcCo){
        printf("Error! rc checkout is %d\n", rcCo);
            exit(-1);
    }
    // create 5 guest threads with unique indexes
    for(int i = 0; i < NUM_GUEST; i++) {
        data[i] = i;
        guest_data_arr[i].id = data[i];
        guest_data_arr[i].money = 0;
        guest_data_arr[i].roomKey = -1;
        guest_data_arr[i].activity = getRandomActivity();
        // can do i just (void*)&guest_data_arr[i]?
        rcG = pthread_create(&guests[i], NULL, guest, (void*)&guest_data_arr[data[i]]);
        if(rcG) {
            printf("Error! rc guest is %d\n", rcG);
            exit(-1);
        }
    }
    
    // wait for all threads to finish
    for(int i=0; i<NUM_GUEST; i++) {
        pthread_join(guests[i], NULL);
    }
    pthread_join(checkinClerk, NULL);
    pthread_join(checkoutClerk, NULL);
    // display the results!
    printHotelResults();
    // terminate program
    pthread_exit(NULL);
}

/**
 * @brief Thread function for each guest that determines order of
 * interactions within the hotel
 * 
 * @param thread_info 
 * @return void* 
 */
void* guest(void* thread_info) {
    //prepare vars
    int g_id;
    struct guest_data *data;
    data = (struct guest_data *)thread_info; 
    g_id = data->id;

    // guests try to check in but there are only 3 rooms
    waitEnterHotel(g_id);
    // guest approach checkin when available
    sem_wait(&talkCheckin);
    printf("Guest %d goes to the check-in reservationist\n", g_id);
    setSharedGuestInfo_In(*data);
    sem_post(&ordercheckin); // signal checkin thread there is a guest

    //wait for checkin to assign room
    sem_wait(&roomAvail);
    printf("Guest %d receives room %d and completes check-in\n",
        shared_guest_in.id, shared_guest_in.roomKey);
    data->roomKey = shared_guest_in.roomKey;
    // checkin is complete and guest is inside the hotel
    sem_post(&talkCheckin);

    // time for an activity!
    guestExecuteHotelActivity(g_id, data->activity);

    // guest approach checkout when available
    sem_wait(&talkCheckout);
    printf("Guest %d goes to the check-out reservationist and returns room %d\n",
        g_id, data->roomKey);
    setSharedGuestInfo_Out(*data);
    sem_post(&orderCheckout); // signal to the checkout clerk

    // wait for checkout to give balance
    sem_wait(&getBalance);
    printf("Guest %d receives the balance of $60\n", g_id);
    printf("Guest %d gives the payment of $60\n", g_id);
    sem_post(&getPayment);
    sem_post(&talkCheckout); // checkout complete, room is freed, guest leave
    sem_wait(&canExit);
    postExitHotel(g_id);
    
    pthread_exit(NULL);
}
/**
 * @brief Set the Shared Guest Info In object so that checkin and guest
 * are synchronized
 * 
 * @param guest 
 */
void setSharedGuestInfo_In(guest_data guest) {
    shared_guest_in = guest;
}
/**
 * @brief Set the SharedGuestInfo Out object so that checkout and guest
 * are synchronized
 * 
 * @param guest 
 */
void setSharedGuestInfo_Out(guest_data guest){
    shared_guest_out = guest;
}
/**
 * @brief Guest must wait to enter hotel if there are no rooms
 * available for them to sleep in
 * 
 * @param guest_id 
 */
void waitEnterHotel(int guest_id) {
    sem_wait(&rooms);
    printf("Guest %d enters hotel\n", guest_id);
}
/**
 * @brief Displays guest is doing activity and increments the counter for
 * that activity.
 * 
 * @param guest_id 
 * @param activity 
 */
void guestExecuteHotelActivity(int guest_id, string activity){
    printf("Guest %d goes to the %s\n", guest_id, activity.c_str());
    for(int i = 0; i < 4; i++) {
        if(activities[i].compare(activity) == 0) {
            activityCounter[i]++;
        }
    }
    sleep((rand() % 3)+1);
}
/**
 * @brief When a guest exits, signal that a new room is available
 * so that a new guest can now enter
 * 
 * @param guest_id 
 */
void postExitHotel(int guest_id) {
    printf("Guest %d exits hotel\n", guest_id);
    sem_post(&rooms);
}
/**
 * @brief Thread function for the check-in clerk
 * 
 * @param thread_info 
 * @return void* 
 */
void* checkin(void* thread_info) {
    int guest_id;
    printf("created checkin clerk. waiting for guests!\n");
    // block if no guests
    for(int i = 0; i<NUM_GUEST; i++) {
        // wait for guest to approach
        sem_wait(&ordercheckin);
        guest_id = shared_guest_in.id;
        greetGuest(guest_id);
        // see room availble and assign the guest to first one
        checkAndAssignRoom();
        sem_post(&roomAvail);
        // guest leaving will unblock thread
    }
    pthread_exit(NULL);
}
/**
 * @brief just prints out greeting to guest
 * 
 * @param guest_id 
 */
void greetGuest(int guest_id){ 
    printf("The check-in reservationist greets Guest %d\n", guest_id);
}
/**
 * @brief This function uses the shared_guest_in information to set the room key
 * It also uses the global roomAvailability so assign a room
 * 
 */
void checkAndAssignRoom() { 
    int flag = 0;
    for(int i = 0; i < roomAvailability.size(); i++){
        if(roomAvailability[i] == 0) {
            // set guest room key to i
            shared_guest_in.roomKey = i;
            // set room to occupied, sem needed to avoid clerks stepping on toes
            sem_wait(&roomDesignation);
            roomAvailability[i] = 1;
            sem_post(&roomDesignation);
            flag = 1;
            break;
        }
    }
    printf("Check-in reservationist assigns room %d to Guest %d\n", 
        shared_guest_in.roomKey, shared_guest_in.id);
}
/**
 * @brief thread function for the check out clerk. Clerks greet guests,
 * collect room keys, and give to and collect charges from the guests.
 * 
 * @param thread_info 
 * @return void* 
 */
void* checkout(void* thread_info) {
    printf("created checkout clerk. waiting for guests!\n");
    // block if no guests
    for(int i = 0; i<NUM_GUEST; i++){
        sem_wait(&orderCheckout);
        checkGuestOut();
        
        printf("The balance for Guest %d is $60\n", shared_guest_out.id);
        sem_post(&getBalance);
        // wait for payment
        sem_wait(&getPayment);
        printf("Recieve $60 payment from Guest %d and complete the check-out\n", 
            shared_guest_out.id);
        sem_post(&canExit);
    }
    pthread_exit(NULL);
}
/**
 * @brief display checkout activity and free the room 
 * 
 */
void checkGuestOut(){
    printf("The check-out reservationist greets Guest %d and receives the key for room %d\n",
            shared_guest_out.id, shared_guest_out.roomKey);
    // set room as available
    sem_wait(&roomDesignation);
    roomAvailability[shared_guest_out.roomKey] = 0;
    sem_post(&roomDesignation);
}
/**
 * @brief Get the Random Activity to assign to a guest
 * 
 * @return string 
 */
string getRandomActivity() {
    // why does 5 not error and 4 never hit pool???
    return activities[(rand() % 5)];
}
/**
 * @brief Helper to initialize all semaphores to the proper value
 * 
 */
void initializeAllSemaphores() {
    sem_init(&rooms, 0, 3); // 3, max rooms 
    sem_init(&talkCheckin, 0, 1); // 1, mutual exlusion talking to reservationist
    sem_init(&ordercheckin, 0, 0); // 0, event ordering - guest first
    sem_init(&roomAvail, 0, 0); // 0, event ordering - checkin first??
    sem_init(&hotelActivity, 0, 0); // 0, occurs after checkin
    sem_init(&talkCheckout, 0, 1); // 1, mut. ex. talk wit clerk
    sem_init(&orderCheckout, 0, 0); // 0, event order - guest first
    sem_init(&roomDesignation, 0, 1); // 1, mut. ex. to avoid concurrent clerk modification)
    sem_init(&getBalance, 0, 0); // 0 clerk first, then guest
    sem_init(&getPayment, 0, 0); // 0 clerk request, guest pays on checkout
    sem_init(&canExit, 0, 0); // 0, clerk says when guest can exit
}
/**
 * @brief Display the counts for num guests and total guests that
 * did each activity
 * 
 */
void printHotelResults() {
    printf("Number of Customers\n");
    printf("\tTotal Guests: %d\n", NUM_GUEST);
    printf("\tPool: %d\n", activityCounter[0]);
    printf("\tRestaurant: %d\n", activityCounter[1]);
    printf("\tFitness Center: %d\n", activityCounter[2]);
    printf("\tBusiness Center: %d\n", activityCounter[3]);
}