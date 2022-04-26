#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#define Tutorial_Slot 1
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_BLUE "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN "\x1b[36m"
#define ANSI_COLOR_RESET "\x1b[0m"

int num_students, num_labs, num_courses;

pthread_mutex_t students_mutex;
pthread_mutex_t labs_mutex;
pthread_mutex_t courses_mutex;

pthread_cond_t students_cond;
pthread_cond_t labs_cond;
pthread_cond_t courses_cond;

typedef struct course course;
typedef struct student student;
typedef struct lab lab;
typedef struct TA TA;

struct course
{
    int id;
    char name[30];
    float interest;
    int course_max_slots;
    int number_of_labs;
    int lab_ids[50];
    int TA_alloted;
    int is_withdrawn;
    int seats_allocated;
};

struct student
{
    int id;
    float calibre;
    int preference_1;
    int preference_2;
    int preference_3;
    int time_taken;
    int current_course;
    int course_opted;
    int is_waiting_for_course_allocation;
    int is_waiting_for_tut;
    int is_waiting_for_opt;
    int out_of_options;
    int current_preference_num;
    int has_exited;
    pthread_cond_t course_allocation_cond;
    pthread_mutex_t slock;
};

struct TA
{
    int id;
    int turns_left;
    int is_busy;
    pthread_mutex_t TA_lock;
};

struct lab
{
    int id;
    char name[30];
    int number_of_TAs;
    int TAship_limit;
    int TAs_exhausted;
    TA TAs[50];
};

student students[50];
lab Labs[20];
course courses[20];

void move_to_next_preference(int id)
{
    if (students[id].current_preference_num == 1)
    {
        students[id].current_preference_num = 2;
        students[id].is_waiting_for_course_allocation = 1;
        students[id].current_course = students[id].preference_2;
        printf(ANSI_COLOR_BLUE "Student %d has changed current preference from %s (priority %d) to %s (priority %d)\n" ANSI_COLOR_RESET, id, courses[students[id].preference_1].name, 1, courses[students[id].preference_2].name, 2);
    }
    else if (students[id].current_preference_num == 2)
    {
        students[id].current_preference_num = 3;
        students[id].is_waiting_for_course_allocation = 1;
        students[id].current_course = students[id].preference_3;
        printf(ANSI_COLOR_BLUE "Student %d has changed current preference from %s (priority %d) to %s (priority %d)\n" ANSI_COLOR_RESET, id, courses[students[id].preference_2].name, 2, courses[students[id].preference_3].name, 3);
    }
    else
    {
        students[id].current_preference_num = -1;
        students[id].current_course = -1;
        students[id].out_of_options = 1;
        students[id].has_exited = 1;
        students[id].is_waiting_for_course_allocation = -1;
        printf(ANSI_COLOR_BLUE "Student %d couldn't get any of their preferred courses\n" ANSI_COLOR_RESET, id);
    }
}

void *student_routine(void *arg)
{
    int id = *(int *)arg;

    // Fill Preference
    sleep(students[id].time_taken);
    printf(ANSI_COLOR_BLUE "Student %d has filled in preferences for course registration\n" ANSI_COLOR_RESET, students[id].id);
    pthread_mutex_lock(&students[id].slock);
    students[id].current_preference_num = 1;
    students[id].current_course = students[id].preference_1;
    students[id].is_waiting_for_course_allocation = 1;
    pthread_mutex_unlock(&students[id].slock);

    while (students[id].out_of_options != 1 && students[id].course_opted == -1)
    {
        pthread_mutex_lock(&students[id].slock);
        if (courses[students[id].current_course].is_withdrawn != 1)
        {
            // wait for course allocation and tut occuring
            pthread_cond_wait(&students[id].course_allocation_cond, &students[id].slock);
        }

        if (students[id].is_waiting_for_opt != 1)
        {
            if (courses[students[id].current_course].is_withdrawn == 1)
            {
                move_to_next_preference(id);
                if (students[id].out_of_options == 1)
                {
                    pthread_mutex_unlock(&students[id].slock);
                    goto student_exit;
                }
                pthread_mutex_unlock(&students[id].slock);
            }
        }
        else
        {
            // decision time
            float probability = (float)rand() / (float)RAND_MAX;
            if (probability <= students[id].calibre * courses[students[id].current_course].interest)
            {
                // Chooses to opt
                students[id].course_opted = students[id].current_course;
                printf(ANSI_COLOR_BLUE "Student %d has selected course %s permanently\n" ANSI_COLOR_RESET, students[id].id, courses[students[id].course_opted].name);
                pthread_mutex_unlock(&students[id].slock);
                goto student_exit;
            }
            else
            {
                // Chooses to move to next
                printf(ANSI_COLOR_BLUE "Student %d has withdrawn from course %s\n" ANSI_COLOR_RESET, students[id].id, courses[students[id].current_course].name);
                move_to_next_preference(students[id].id);
                if (students[id].out_of_options == 1)
                {
                    pthread_mutex_unlock(&students[id].slock);
                    goto student_exit;
                }
            }
            pthread_mutex_unlock(&students[id].slock);
        }
    }

student_exit:
    students[id].has_exited = 1;
    free(arg);
}

void *course_routine(void *arg)
{
    sleep(5);
    int id = *(int *)arg;

    while (courses[id].is_withdrawn != 1)
    {
        // Update withdrawal status
        int check = 0;
        for (int i = 0; i < courses[id].number_of_labs; i++)
        {
            if (Labs[courses[id].lab_ids[i]].TAs_exhausted != 1)
            {
                for (int j = 0; j < Labs[courses[id].lab_ids[i]].number_of_TAs; j++)
                {
                    if (Labs[courses[id].lab_ids[i]].TAs[j].turns_left > 0)
                    {
                        check = 1;
                        break;
                    }
                }
            }
            if (check == 1)
            {
                break;
            }
        }
        if (check == 0)
        {
            printf(ANSI_COLOR_GREEN "Course %s doesn’t have any TA’s eligible and is removed from course offerings\n" ANSI_COLOR_RESET, courses[id].name);
            courses[id].is_withdrawn = 1;

            for (int t = 0; t < num_students; t++)
            {
                if (students[t].current_course == id)
                    pthread_cond_signal(&students[t].course_allocation_cond);
            }

            goto course_withdrawal;
        }

        courses[id].seats_allocated = rand() % courses[id].course_max_slots + 1;
        printf(ANSI_COLOR_GREEN "Course %s has been allocated %d seats\n" ANSI_COLOR_RESET, courses[id].name, courses[id].seats_allocated);
        // Allot TA if not present
        if (courses[id].TA_alloted == -1)
        {
            for (int i = 0; i < courses[id].number_of_labs; i++)
            {
                if (Labs[courses[id].lab_ids[i]].TAs_exhausted != 1)
                {
                    for (int j = 0; j < Labs[courses[id].lab_ids[i]].number_of_TAs; j++)
                    {
                        if (Labs[courses[id].lab_ids[i]].TAs[j].turns_left > 0)
                        {
                            if (Labs[courses[id].lab_ids[i]].TAs[j].is_busy != 1)
                            {
                                pthread_mutex_lock(&Labs[courses[id].lab_ids[i]].TAs[j].TA_lock);
                                courses[id].TA_alloted = Labs[courses[id].lab_ids[i]].TAs[j].id;
                                Labs[courses[id].lab_ids[i]].TAs[j].is_busy = 1;
                                Labs[courses[id].lab_ids[i]].TAs[j].turns_left--;
                                printf(ANSI_COLOR_GREEN "TA %d from lab %s has been allocated to course %s for his %dth TA ship.\n" ANSI_COLOR_RESET, j, Labs[courses[id].lab_ids[i]].name, courses[id].name, Labs[courses[id].lab_ids[i]].TAship_limit - Labs[courses[id].lab_ids[i]].TAs[j].turns_left);

                                // allocate students for tut
                                int filled_slots = 0;
                                while (filled_slots == 0)
                                {
                                    for (int i = 0; i < num_students; i++)
                                    {
                                        if (students[i].current_course == courses[id].id && students[i].is_waiting_for_course_allocation == 1 && students[i].has_exited != 1)
                                        {
                                            pthread_mutex_lock(&students[i].slock);
                                            printf(ANSI_COLOR_BLUE "Student %d has been allocated a seat in course %s" ANSI_COLOR_RESET "\n", students[i].id, courses[id].name);
                                            filled_slots++;
                                            students[i].is_waiting_for_course_allocation = -1;
                                            students[i].is_waiting_for_tut = 1;
                                        }
                                        if (filled_slots == courses[id].seats_allocated)
                                        {
                                            break;
                                        }
                                    }
                                    sleep(1);
                                }

                                // start tut
                                printf(ANSI_COLOR_GREEN "Tutorial has started for Course %s with %d seats filled out of %d\n" ANSI_COLOR_RESET, courses[id].name, filled_slots, courses[id].seats_allocated);
                                sleep(Tutorial_Slot);
                                printf(ANSI_COLOR_MAGENTA "TA %d from lab %s has completed the tutorial and left the course %s\n" ANSI_COLOR_RESET, j, Labs[courses[id].lab_ids[i]].name, courses[id].name);
                                Labs[courses[id].lab_ids[i]].TAs[j].is_busy = 0;
                                courses[id].TA_alloted = -1;

                                for (int i = 0; i < num_students; i++)
                                {
                                    if (students[i].current_course == id && students[i].is_waiting_for_course_allocation == -1)
                                    {
                                        students[i].is_waiting_for_opt = 1;
                                        students[i].is_waiting_for_tut = -1;
                                        pthread_mutex_unlock(&students[i].slock);
                                        pthread_cond_signal(&students[i].course_allocation_cond);
                                    }
                                }

                                // check if Lab exhausted
                                if (Labs[courses[id].lab_ids[i]].TAs_exhausted != 1)
                                {
                                    int flag = 0;
                                    for (int a = 0; a < Labs[courses[id].lab_ids[i]].number_of_TAs; a++)
                                    {
                                        if (Labs[courses[id].lab_ids[i]].TAs[a].turns_left > 0)
                                        {
                                            flag = 1;
                                            break;
                                        }
                                    }
                                    if (flag == 0)
                                    {
                                        printf(ANSI_COLOR_MAGENTA "Lab %s no longer has students available for TA ship\n" ANSI_COLOR_RESET, Labs[courses[id].lab_ids[i]].name);
                                        Labs[courses[id].lab_ids[i]].TAs_exhausted = 1;
                                    }
                                }
                                pthread_mutex_unlock(&Labs[courses[id].lab_ids[i]].TAs[j].TA_lock);
                            }
                        }
                    }
                }
            }
        }
    }
course_withdrawal:
    free(arg);
}

int main()
{
    srand(time(NULL));
    scanf("%d %d %d", &num_students, &num_labs, &num_courses);
    for (int i = 0; i < num_courses; i++)
    {
        courses[i].id = i;
        scanf("%s %f %d %d", courses[i].name, &courses[i].interest, &courses[i].course_max_slots, &courses[i].number_of_labs);
        for (int j = 0; j < courses[i].number_of_labs; j++)
        {
            scanf("%d", &courses[i].lab_ids[j]);
        }
        courses[i].TA_alloted = -1;
        courses[i].is_withdrawn = -1;
        courses[i].seats_allocated = -1;
    }
    for (int i = 0; i < num_students; i++)
    {
        students[i].id = i;
        scanf("%f %d %d %d %d", &students[i].calibre, &students[i].preference_1, &students[i].preference_2, &students[i].preference_3, &students[i].time_taken);
        students[i].current_course = -1;
        students[i].course_opted = -1;
        students[i].is_waiting_for_tut = -1;
        students[i].is_waiting_for_opt = -1;
        students[i].out_of_options = -1;
        students[i].current_preference_num = -1;
        students[i].is_waiting_for_course_allocation = -1;
        students[i].has_exited = -1;
    }
    for (int i = 0; i < num_labs; i++)
    {
        Labs[i].id = i;
        scanf("%s %d %d", Labs[i].name, &Labs[i].number_of_TAs, &Labs[i].TAship_limit);
        Labs[i].TAs_exhausted = -1;
        for (int j = 0; j < Labs[i].number_of_TAs; j++)
        {
            Labs[i].TAs[j].id = j;
            Labs[i].TAs[j].turns_left = Labs[i].TAship_limit;
            Labs[i].TAs[j].is_busy = -1;
        }
    }

    pthread_t students_threads[num_students];
    pthread_t courses_threads[num_courses];

    for (int i = 0; i < num_students; i++)
    {
        pthread_mutex_init(&students[i].slock, NULL);
        pthread_cond_init(&students[i].course_allocation_cond, NULL);

        int *sid = (int *)malloc(sizeof(int));
        *sid = i;
        pthread_create(&students_threads[i], NULL, (void *)&student_routine, sid);
    }

    for (int i = 0; i < num_courses; i++)
    {
        int *cid = (int *)malloc(sizeof(int));
        *cid = i;
        pthread_create(&courses_threads[i], NULL, (void *)&course_routine, cid);
    }

    for (int i = 0; i < num_students; i++)
    {
        pthread_join(students_threads[i], NULL);
    }

    for (int i = 0; i < num_students; i++)
    {

        pthread_mutex_destroy(&students[i].slock);
        pthread_cond_destroy(&students[i].course_allocation_cond);
    }
}
