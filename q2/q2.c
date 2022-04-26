#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <semaphore.h>
#include <errno.h>
#include <string.h>

#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_RESET "\x1b[0m"

typedef struct Zone Zone;
typedef struct spectator spectator;
typedef struct goalscoring_chance goalscoring_chance;
typedef struct group group;

sem_t H_Empty, N_Empty, A_Empty;
sem_t HN, HNA, A;
pthread_mutex_t A_lock, H_lock, N_lock;
pthread_mutex_t Hgoal_lock, Agoal_lock;
pthread_mutex_t group_lock[100];
pthread_cond_t Hgoal_cond, Agoal_cond;
pthread_cond_t group_cond[100];

int H_goals = 0, A_goals = 0;
int total_spectators = 0;
int group_members_at_exit[100];

time_t start_time;
time_t current_time;

time_t time_from_start()
{
    current_time = time(NULL);
    return current_time - start_time;
}

struct Zone
{
    int capacity;
    int currently_filled;
};
struct spectator
{
    char name[20];
    char type;
    int time_of_arrival;
    int patience;
    int time_of_seating;
    int enrage_limit;
    int is_waiting_for_a_seat;
    int is_seated;
    int is_at_exit;
    char seated_zone;
    int group_id;
};
struct group
{
    int group_size;
    int group_id;
    spectator *spectators;
};
struct goalscoring_chance
{
    char type;
    int time_since_start;
    float probability;
};

Zone Hzone, Nzone, Azone;
int spectating_time, num_groups, num_chances;

void *spectator_routine(void *arg)
{
    spectator s = *(spectator *)arg;
    // Arrives at the stadium
    sleep(s.time_of_arrival);
    printf(ANSI_COLOR_RED "t=%ld : %s has reached the stadium\n" ANSI_COLOR_RESET, time_from_start(), s.name);

    // waiting for a seat
    s.is_waiting_for_a_seat = 1;

    struct timespec ts1, ts2;
    if (clock_gettime(CLOCK_REALTIME, &ts1) == -1)
    {
        perror("clock gettime");
        exit(EXIT_FAILURE);
    }

    ts1.tv_sec += s.patience;
    if (s.type == 'H')
    {
        int k; // Home, Neutral
        while ((k = sem_timedwait(&HN, &ts1)) == -1 && errno != ETIMEDOUT)
        {
            continue;
        }
        if ((k == -1) && errno == ETIMEDOUT)
        {
            printf(ANSI_COLOR_MAGENTA "t=%ld : %s could not get a seat\n" ANSI_COLOR_RESET, time_from_start(), s.name);
            s.is_waiting_for_a_seat = 0;
            s.is_seated = 0;
            goto exit_gate;
        }
        else if (k == 0)
        {
            char seat_zone;
            pthread_mutex_lock(&H_lock);
            pthread_mutex_lock(&N_lock);
            int val;
            if (Hzone.capacity - Hzone.currently_filled > 0 && Nzone.capacity - Nzone.currently_filled > 0)
            {
                int random_number = rand() % 2;
                if (random_number == 0)
                {
                    seat_zone = 'H';
                    Hzone.currently_filled++;
                    sem_getvalue(&HNA, &val);
                    if (val > 0)
                        sem_wait(&HNA);
                }
                else
                {
                    seat_zone = 'N';
                    Nzone.currently_filled++;
                    sem_getvalue(&HNA, &val);
                    if (val > 0)
                        sem_wait(&HNA);
                }
            }
            else if (Hzone.capacity - Hzone.currently_filled > 0)
            {
                seat_zone = 'H';
                Hzone.currently_filled++;
                sem_getvalue(&HNA, &val);
                if (val > 0)
                    sem_wait(&HNA);
            }
            else if (Nzone.capacity - Nzone.currently_filled > 0)
            {
                seat_zone = 'N';
                Nzone.currently_filled++;
                sem_getvalue(&HNA, &val);
                if (val > 0)
                    sem_wait(&HNA);
            }
            pthread_mutex_unlock(&N_lock);
            pthread_mutex_unlock(&H_lock);
            s.is_seated = 1;
            s.is_waiting_for_a_seat = 0;
            s.time_of_seating = time_from_start();
            s.seated_zone = seat_zone;
            printf(ANSI_COLOR_MAGENTA "t=%ld : %s has got a seat in zone %c\n" ANSI_COLOR_RESET, time_from_start(), s.name, s.seated_zone);
        }

        // Watching the match
        if (clock_gettime(CLOCK_REALTIME, &ts2) == -1)
        {
            perror("clock gettime");
            exit(EXIT_FAILURE);
        }
        ts2.tv_sec += spectating_time;
        if (s.type == 'H')
        {
            int j;
            pthread_mutex_lock(&Agoal_lock);
            int t0 = time_from_start();
            while (A_goals < s.enrage_limit && j != ETIMEDOUT)
                j = pthread_cond_timedwait(&Agoal_cond, &Agoal_lock, &ts2);
            int t1 = time_from_start();
            pthread_mutex_unlock(&Agoal_lock);
            if ((t1 - t0) >= spectating_time || j == ETIMEDOUT)
            {
                // Timed out
                // stands up from his/her seat
                s.is_seated = 0;
                printf(ANSI_COLOR_GREEN "t=%ld : %s watched the match for %d seconds and is leaving\n" ANSI_COLOR_RESET, time_from_start(), s.name, spectating_time);
            }
            else
            {
                // Gets Enraged
                printf(ANSI_COLOR_GREEN "t=%ld : %s is leaving due to bad performance of his team\n" ANSI_COLOR_RESET, time_from_start(), s.name);
                s.is_seated = 0;
            }
        }
        else if (s.type == 'A')
        {
            int j;
            pthread_mutex_lock(&Hgoal_lock);
            int t0 = time_from_start();
            while (H_goals < s.enrage_limit && j != ETIMEDOUT)
                j = pthread_cond_timedwait(&Hgoal_cond, &Hgoal_lock, &ts2);
            int t1 = time_from_start();
            pthread_mutex_unlock(&Hgoal_lock);
            if ((t1 - t0) >= spectating_time || j == ETIMEDOUT)
            {
                // Timed out
                // stands up from his/her seat
                s.is_seated = 0;
                printf(ANSI_COLOR_GREEN "t=%ld : %s watched the match for %d seconds and is leaving\n" ANSI_COLOR_RESET, time_from_start(), s.name, spectating_time);
            }
            else
            {
                // Gets Enraged
                printf(ANSI_COLOR_GREEN "t=%ld : %s is leaving due to bad performance of his team\n" ANSI_COLOR_RESET, time_from_start(), s.name);
                s.is_seated = 0;
            }
        }
        else // Neutral fan
        {
            sleep(spectating_time);
            s.is_seated = 0;
            printf(ANSI_COLOR_GREEN "t=%ld : %s watched the match for %d seconds and is leaving\n" ANSI_COLOR_RESET, time_from_start(), s.name, spectating_time);
        }
        if (s.seated_zone == 'H')
        {
            pthread_mutex_lock(&H_lock);
            Hzone.currently_filled--;
            pthread_mutex_unlock(&H_lock);
            sem_post(&HN);
            sem_post(&HNA);
        }
        else
        {
            pthread_mutex_lock(&N_lock);
            Nzone.currently_filled--;
            pthread_mutex_unlock(&N_lock);
            sem_post(&HNA);
            sem_post(&HN);
        }
    }

    if (s.type == 'N')
    {
        int k; // Home, Neutral
        while ((k = sem_timedwait(&HNA, &ts1)) == -1 && errno != ETIMEDOUT)
        {
            continue;
        }
        int val;
        if ((k == -1) && errno == ETIMEDOUT)
        {
            printf(ANSI_COLOR_MAGENTA "t=%ld : %s could not get a seat\n" ANSI_COLOR_RESET, time_from_start(), s.name);
            s.is_waiting_for_a_seat = 0;
            s.is_seated = 0;
            goto exit_gate;
        }
        else if (k == 0)
        {
            char seat_zone;
            pthread_mutex_lock(&H_lock);
            pthread_mutex_lock(&N_lock);
            pthread_mutex_lock(&A_lock);
            if (Hzone.capacity - Hzone.currently_filled > 0 && Nzone.capacity - Nzone.currently_filled > 0 && Azone.capacity - Azone.currently_filled > 0)
            {
                int random_number = rand() % 3;
                if (random_number == 0)
                {
                    seat_zone = 'H';
                    Hzone.currently_filled++;
                    sem_getvalue(&HN, &val);
                    if (val > 0)
                        sem_wait(&HN);
                }
                else if (random_number == 1)
                {
                    seat_zone = 'N';
                    Nzone.currently_filled++;
                    sem_getvalue(&HN, &val);
                    if (val > 0)
                        sem_wait(&HN);
                }
                else
                {
                    seat_zone = 'A';
                    Azone.currently_filled++;
                    sem_getvalue(&A, &val);
                    if (val > 0)
                        sem_wait(&A);
                }
            }
            else if (Hzone.capacity - Hzone.currently_filled > 0 && Nzone.capacity - Nzone.currently_filled > 0)
            {
                int random_number = rand() % 2;
                if (random_number == 0)
                {
                    seat_zone = 'H';
                    Hzone.currently_filled++;
                    sem_getvalue(&HN, &val);
                    if (val > 0)
                        sem_wait(&HN);
                }
                else
                {
                    seat_zone = 'N';
                    Nzone.currently_filled++;
                    sem_getvalue(&HN, &val);
                    if (val > 0)
                        sem_wait(&HN);
                }
            }
            else if (Hzone.capacity - Hzone.currently_filled > 0 && Azone.capacity - Azone.currently_filled > 0)
            {
                int random_number = rand() % 2;
                if (random_number == 0)
                {
                    seat_zone = 'H';
                    Hzone.currently_filled++;
                    sem_getvalue(&HN, &val);
                    if (val > 0)
                        sem_wait(&HN);
                }
                else
                {
                    seat_zone = 'A';
                    Nzone.currently_filled++;
                    sem_getvalue(&A, &val);
                    if (val > 0)
                        sem_wait(&A);
                }
            }
            else if (Nzone.capacity - Nzone.currently_filled > 0 && Azone.capacity - Azone.currently_filled > 0)
            {
                int random_number = rand() % 2;
                if (random_number == 0)
                {
                    seat_zone = 'N';
                    Nzone.currently_filled++;
                    sem_getvalue(&HN, &val);
                    if (val > 0)
                        sem_wait(&HN);
                }
                else
                {
                    seat_zone = 'A';
                    Azone.currently_filled++;
                    sem_getvalue(&A, &val);
                    if (val > 0)
                        sem_wait(&A);
                }
            }
            else if (Hzone.capacity - Hzone.currently_filled > 0)
            {
                seat_zone = 'H';
                Hzone.currently_filled++;
                sem_getvalue(&HN, &val);
                if (val > 0)
                    sem_wait(&HN);
            }
            else if (Nzone.capacity - Nzone.currently_filled > 0)
            {
                seat_zone = 'N';
                Nzone.currently_filled++;
                sem_getvalue(&HN, &val);
                if (val > 0)
                    sem_wait(&HN);
            }
            else if (Azone.capacity - Azone.currently_filled > 0)
            {
                seat_zone = 'A';
                Azone.currently_filled++;
                sem_getvalue(&A, &val);
                if (val > 0)
                    sem_wait(&A);
            }
            pthread_mutex_unlock(&A_lock);
            pthread_mutex_unlock(&N_lock);
            pthread_mutex_unlock(&H_lock);
            s.is_seated = 1;
            s.is_waiting_for_a_seat = 0;
            s.time_of_seating = time_from_start();
            s.seated_zone = seat_zone;
            printf(ANSI_COLOR_MAGENTA "t=%ld : %s has got a seat in zone %c\n" ANSI_COLOR_RESET, time_from_start(), s.name, s.seated_zone);
        }

        // Watching the match
        if (clock_gettime(CLOCK_REALTIME, &ts2) == -1)
        {
            perror("clock gettime");
            exit(EXIT_FAILURE);
        }
        ts2.tv_sec += spectating_time;
        if (s.type == 'H')
        {
            int j;
            pthread_mutex_lock(&Agoal_lock);
            int t0 = time_from_start();
            while (A_goals < s.enrage_limit && j != ETIMEDOUT)
                j = pthread_cond_timedwait(&Agoal_cond, &Agoal_lock, &ts2);
            int t1 = time_from_start();
            pthread_mutex_unlock(&Agoal_lock);
            if ((t1 - t0) >= spectating_time || j == ETIMEDOUT)
            {
                // Timed out
                // stands up from his/her seat
                s.is_seated = 0;
                printf(ANSI_COLOR_GREEN "t=%ld : %s watched the match for %d seconds and is leaving\n" ANSI_COLOR_RESET, time_from_start(), s.name, spectating_time);
            }
            else
            {
                // Gets Enraged
                printf(ANSI_COLOR_GREEN "t=%ld : %s is leaving due to bad performance of his team\n" ANSI_COLOR_RESET, time_from_start(), s.name);
                s.is_seated = 0;
            }
        }
        else if (s.type == 'A')
        {
            int j;
            pthread_mutex_lock(&Hgoal_lock);
            int t0 = time_from_start();
            while (H_goals < s.enrage_limit && j != ETIMEDOUT)
                j = pthread_cond_timedwait(&Hgoal_cond, &Hgoal_lock, &ts2);
            int t1 = time_from_start();
            pthread_mutex_unlock(&Hgoal_lock);
            if ((t1 - t0) >= spectating_time || j == ETIMEDOUT)
            {
                // Timed out
                // stands up from his/her seat
                s.is_seated = 0;
                printf(ANSI_COLOR_GREEN "t=%ld : %s watched the match for %d seconds and is leaving\n" ANSI_COLOR_RESET, time_from_start(), s.name, spectating_time);
            }
            else
            {
                // Gets Enraged
                printf(ANSI_COLOR_GREEN "t=%ld : %s is leaving due to bad performance of his team\n" ANSI_COLOR_RESET, time_from_start(), s.name);
                s.is_seated = 0;
            }
        }
        else // Neutral fan
        {
            sleep(spectating_time);
            s.is_seated = 0;
            printf(ANSI_COLOR_GREEN "t=%ld : %s watched the match for %d seconds and is leaving\n" ANSI_COLOR_RESET, time_from_start(), s.name, spectating_time);
        }
        if (s.seated_zone == 'H')
        {
            pthread_mutex_lock(&H_lock);
            Hzone.currently_filled--;
            pthread_mutex_unlock(&H_lock);
            sem_post(&HN);
            sem_post(&HNA);
        }
        else if (s.seated_zone == 'N')
        {
            pthread_mutex_lock(&N_lock);
            Nzone.currently_filled--;
            pthread_mutex_unlock(&N_lock);
            sem_post(&HN);
            sem_post(&HNA);
        }
        else
        {
            pthread_mutex_lock(&A_lock);
            Azone.currently_filled--;
            pthread_mutex_unlock(&A_lock);
            sem_post(&A);
            sem_post(&HNA);
        }
    }

    if (s.type == 'A')
    {
        int k; // Away

        while ((k = sem_timedwait(&A, &ts1)) == -1 && errno != ETIMEDOUT)
        {
            continue;
        }
        int val;
        if ((k == -1) && errno == ETIMEDOUT)
        {
            printf(ANSI_COLOR_MAGENTA "t=%ld : %s could not get a seat\n" ANSI_COLOR_RESET, time_from_start(), s.name);
            s.is_waiting_for_a_seat = 0;
            s.is_seated = 0;
            goto exit_gate;
        }
        else if (k == 0)
        {
            pthread_mutex_lock(&A_lock);
            Azone.currently_filled++;
            pthread_mutex_unlock(&A_lock);
            sem_getvalue(&HNA, &val);
            if (val > 0)
                sem_wait(&HNA);
            s.is_seated = 1;
            s.is_waiting_for_a_seat = 0;
            s.time_of_seating = time_from_start();
            s.seated_zone = 'A';
            printf(ANSI_COLOR_MAGENTA "t=%ld : %s has got a seat in zone %c\n" ANSI_COLOR_RESET, time_from_start(), s.name, s.seated_zone);
        }

        // Watching the match
        if (clock_gettime(CLOCK_REALTIME, &ts2) == -1)
        {
            perror("clock gettime");
            exit(EXIT_FAILURE);
        }
        ts2.tv_sec += spectating_time;
        if (s.type == 'H')
        {
            int j;
            pthread_mutex_lock(&Agoal_lock);
            int t0 = time_from_start();
            while (A_goals < s.enrage_limit && j != ETIMEDOUT)
                j = pthread_cond_timedwait(&Agoal_cond, &Agoal_lock, &ts2);
            int t1 = time_from_start();
            pthread_mutex_unlock(&Agoal_lock);
            if ((t1 - t0) >= spectating_time || j == ETIMEDOUT)
            {
                // Timed out
                // stands up from his/her seat
                s.is_seated = 0;
                printf(ANSI_COLOR_GREEN "t=%ld : %s watched the match for %d seconds and is leaving\n" ANSI_COLOR_RESET, time_from_start(), s.name, spectating_time);
            }
            else
            {
                // Gets Enraged
                printf(ANSI_COLOR_GREEN "t=%ld : %s is leaving due to bad performance of his team\n" ANSI_COLOR_RESET, time_from_start(), s.name);
                s.is_seated = 0;
            }
        }
        else if (s.type == 'A')
        {
            int j;
            pthread_mutex_lock(&Hgoal_lock);
            int t0 = time_from_start();
            while (H_goals < s.enrage_limit && j != ETIMEDOUT)
                j = pthread_cond_timedwait(&Hgoal_cond, &Hgoal_lock, &ts2);
            int t1 = time_from_start();
            pthread_mutex_unlock(&Hgoal_lock);
            if ((t1 - t0) >= spectating_time || j == ETIMEDOUT)
            {
                // Timed out
                // stands up from his/her seat
                s.is_seated = 0;
                printf(ANSI_COLOR_GREEN "t=%ld : %s watched the match for %d seconds and is leaving\n" ANSI_COLOR_RESET, time_from_start(), s.name, spectating_time);
            }
            else
            {
                // Gets Enraged
                printf(ANSI_COLOR_GREEN "t=%ld : %s is leaving due to bad performance of his team\n" ANSI_COLOR_RESET, time_from_start(), s.name);
                s.is_seated = 0;
            }
        }
        else // Neutral fan
        {
            sleep(spectating_time);
            s.is_seated = 0;
            printf(ANSI_COLOR_GREEN "t=%ld : %s watched the match for %d seconds and is leaving\n" ANSI_COLOR_RESET, time_from_start(), s.name, spectating_time);
        }

        sem_post(&A);
        sem_post(&HNA);
    }

exit_gate:
    // Exit the stadium
    s.is_at_exit = 1;
    printf(ANSI_COLOR_BLUE "t=%ld : %s is waiting for their friends at the exit\n" ANSI_COLOR_RESET, time_from_start(), s.name);
    pthread_mutex_lock(&group_lock[s.group_id]);
    group_members_at_exit[s.group_id]++;
    // Signals the exit_routine
    pthread_cond_broadcast(&group_cond[s.group_id]);
    pthread_mutex_unlock(&group_lock[s.group_id]);
}

void *goals_routine(void *args)
{
    goalscoring_chance *g = (goalscoring_chance *)args;
    // printf("Num Chances %d\n", num_chances);
    for (int i = 0; i < num_chances; i++)
    {
        // wait for goalscoring chance to come
        int current_time = time_from_start();
        int time_to_wait = g[i].time_since_start - current_time;
        if (time_to_wait > 0)
            sleep(time_to_wait);

        // Now the chance has come
        float probability = (float)rand() / (float)RAND_MAX;
        // Goal scored
        char key[10];
        if (g[i].type == 'H')
        {
            pthread_mutex_lock(&Hgoal_lock);
            if (H_goals == 0)
                strcpy(key, "1st");
            else if (H_goals == 1)
                strcpy(key, "2nd");
            else if (H_goals == 2)
                strcpy(key, "3rd");
            else if (H_goals == 3)
            {
                sprintf(key, "%dth", H_goals + 1);
            }
            pthread_mutex_unlock(&Hgoal_lock);
        }
        else
        {
            pthread_mutex_lock(&Agoal_lock);
            if (A_goals == 0)
                strcpy(key, "1st");
            else if (A_goals == 1)
                strcpy(key, "2nd");
            else if (A_goals == 2)
                strcpy(key, "3rd");
            else if (A_goals == 3)
            {
                sprintf(key, "%dth", A_goals + 1);
            }
            pthread_mutex_unlock(&Agoal_lock);
        }
        if (probability <= g[i].probability)
        {
            printf("t=%ld : Team %c has scored their %s goal\n", time_from_start(), g[i].type, key);
            if (g[i].type == 'H')
            {
                pthread_mutex_lock(&Hgoal_lock);
                H_goals++;
                pthread_cond_broadcast(&Hgoal_cond);
                pthread_mutex_unlock(&Hgoal_lock);
            }
            else
            {
                pthread_mutex_lock(&Agoal_lock);
                A_goals++;
                pthread_cond_broadcast(&Agoal_cond);
                pthread_mutex_unlock(&Agoal_lock);
            }
        }
        else
        {
            printf("t=%ld : Team %c missed the chance to score their %s goal\n", time_from_start(), g[i].type, key);
        }
    }
}

void *group_exit_routine(void *args)
{
    group *g = (group *)args;

    // Wait for all spectators of a group to leave
    pthread_mutex_lock(&group_lock[g->group_id]);
    while (group_members_at_exit[g->group_id] < g->group_size)
        pthread_cond_wait(&group_cond[g->group_id], &group_lock[g->group_id]);
    pthread_mutex_unlock(&group_lock[g->group_id]);

    // Now the time has come
    printf(ANSI_COLOR_YELLOW "t=%ld : Group %d is leaving for dinner\n" ANSI_COLOR_RESET, time_from_start(), (g->group_id + 1));
}

int main()
{
    srand(time(NULL));
    for (int i = 0; i < 100; i++)
    {
        group_members_at_exit[i] = 0;
    }
    scanf("%d %d %d", &Hzone.capacity, &Nzone.capacity, &Azone.capacity);
    scanf("%d", &spectating_time);
    scanf("%d", &num_groups);
    group groups[num_groups];
    group *group_arr = (group *)malloc(sizeof(group) * num_groups);
    for (int i = 0; i < num_groups; i++)
    {
        scanf("%d", &groups[i].group_size);
        groups[i].group_id = i;
        groups[i].spectators = (spectator *)malloc(sizeof(spectator) * groups[i].group_size);
        for (int j = 0; j < groups[i].group_size; j++)
        {
            groups[i].spectators[j].group_id = i;
            groups[i].spectators[j].time_of_seating = -1;
            groups[i].spectators[j].is_waiting_for_a_seat = 0;
            groups[i].spectators[j].is_seated = 0;
            groups[i].spectators[j].is_at_exit = 0;
            groups[i].spectators[j].seated_zone = 'A';
            scanf("%s %c %d %d %d", groups[i].spectators[j].name, &groups[i].spectators[j].type, &groups[i].spectators[j].time_of_arrival, &groups[i].spectators[j].patience, &groups[i].spectators[j].enrage_limit);
        }
        group_arr[i] = groups[i];
    }
    scanf("%d", &num_chances);
    goalscoring_chance chances[num_chances];
    goalscoring_chance *chance_arr = (goalscoring_chance *)malloc(sizeof(goalscoring_chance) * num_chances);
    for (int i = 0; i < num_chances; i++)
    {
        scanf(" %c %d %f", &chances[i].type, &chances[i].time_since_start, &chances[i].probability);
        chance_arr[i] = chances[i];
    }

    pthread_t spectator_threads[total_spectators];
    pthread_t Goals;
    pthread_t groups_exit[num_groups];
    sem_init(&HN, 0, Hzone.capacity + Nzone.capacity);
    sem_init(&HNA, 0, Hzone.capacity + Nzone.capacity + Azone.capacity);
    sem_init(&A, 0, Azone.capacity);
    pthread_mutex_init(&H_lock, NULL); // Lock for accessing zone
    pthread_mutex_init(&N_lock, NULL);
    pthread_mutex_init(&A_lock, NULL);
    pthread_mutex_init(&Hgoal_lock, NULL); // Lock for accesing goals
    pthread_mutex_init(&Agoal_lock, NULL);
    pthread_cond_init(&Hgoal_cond, NULL);
    pthread_cond_init(&Agoal_cond, NULL);
    for (int i = 0; i < num_groups; i++)
    {
        pthread_mutex_init(&group_lock[i], NULL);
        pthread_cond_init(&group_cond[i], NULL);
    }

    start_time = time(NULL);
    for (int i = 0; i < num_groups; i++)
    {
        pthread_create(&groups_exit[i], NULL, group_exit_routine, (void *)&group_arr[i]);
    }
    for (int i = 0; i < num_groups; i++)
    {
        for (int j = 0; j < groups[i].group_size; j++)
        {
            pthread_create(&spectator_threads[total_spectators], NULL, spectator_routine, &groups[i].spectators[j]);
            total_spectators++;
        }
    }

    pthread_create(&Goals, NULL, goals_routine, (void *)chance_arr);

    for (int i = 0; i < total_spectators; i++)
    {
        pthread_join(spectator_threads[i], NULL);
    }
    pthread_join(Goals, NULL);
    for (int i = 0; i < num_groups; i++)
    {
        pthread_join(groups_exit[i], NULL);
    }

    sem_destroy(&HN);
    sem_destroy(&HNA);
    sem_destroy(&A);
    pthread_mutex_destroy(&H_lock);
    pthread_mutex_destroy(&N_lock);
    pthread_mutex_destroy(&A_lock);
    pthread_mutex_destroy(&Hgoal_lock);
    pthread_mutex_destroy(&Agoal_lock);
    pthread_cond_destroy(&Hgoal_cond);
    pthread_cond_destroy(&Agoal_cond);
    for (int i = 0; i < num_groups; i++)
    {
        pthread_mutex_destroy(&group_lock[i]);
        pthread_cond_destroy(&group_cond[i]);
    }

    for (int i = 0; i < num_groups; i++)
    {
        free(groups[i].spectators);
    }
    free(chance_arr);
    return 0;
}