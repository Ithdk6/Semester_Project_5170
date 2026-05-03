#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

#define MAX_TASKS 50
#define MAX_TIME 10000
#define NUM_WORKLOAD_SCENARIOS 5

//task struct
typedef struct {
    int id;
    int arrival_time;
    int execution_time;
    int remaining_time;
    int deadline;
    int priority;  // 0 = critical, 1 = non-critical
    int completion_time;
    int start_time;
    int response_time;
    int turnaround_time;
    int waiting_time;
    bool completed;
    bool deadline_met;
    int preemption_count;
} Task;

//metrics struct
typedef struct {
    int total_tasks;
    int completed_tasks;
    int deadline_misses;
    int dropped_jobs;
    double avg_response_time;
    double avg_turnaround_time;
    double avg_waiting_time;
    double avg_slack_time;
    double cpu_utilization;
    int total_preemptions;
    int critical_deadline_misses;
    int noncritical_deadline_misses;
} Metrics;

void generate_task_set(Task tasks[], int *num_tasks, double utilization);
void reset_tasks(Task tasks[], int num_tasks);
void simulate_srtf(Task tasks[], int num_tasks, Metrics *metrics);
void simulate_modified_srtf(Task tasks[], int num_tasks, Metrics *metrics);
int find_next_task_srtf(Task tasks[], int num_tasks, int current_time);
int find_next_task_modified(Task tasks[], int num_tasks, int current_time);
void calculate_metrics(Task tasks[], int num_tasks, Metrics *metrics, int total_time);
void print_metrics(const char *scheduler_name, Metrics *metrics, double utilization);
void print_task_details(Task tasks[], int num_tasks, const char *scheduler_name);
void export_csv_results(FILE *fp, const char *scheduler, double util, Metrics *metrics);

int main() {
    srand(time(NULL));

    double utilization_levels[NUM_WORKLOAD_SCENARIOS] = {0.5, 0.7, 0.85, 0.95, 1.1};

    FILE *csv_file = fopen("../data/simulation_results.csv", "w");
    if (csv_file == NULL) {
        fprintf(stderr, "Error: Could not create CSV file\n");
        return 1;
    }
    fprintf(csv_file, "Scheduler,Utilization,TotalTasks,CompletedTasks,DeadlineMisses,");
    fprintf(csv_file, "CriticalMisses,NonCriticalMisses,DroppedJobs,AvgResponseTime,");
    fprintf(csv_file, "AvgTurnaroundTime,AvgWaitingTime,AvgSlackTime,CPUUtilization,");
    fprintf(csv_file, "TotalPreemptions\n");



    for (int scenario = 0; scenario < NUM_WORKLOAD_SCENARIOS; scenario++) {
        double target_util = utilization_levels[scenario];

        //generate task set
        Task tasks[MAX_TASKS];
        int num_tasks;
        generate_task_set(tasks, &num_tasks, target_util);

        printf("Generated %d tasks for this scenario\n\n", num_tasks);

        //test standard SRTF
        Task tasks_srtf[MAX_TASKS];
        memcpy(tasks_srtf, tasks, sizeof(Task) * num_tasks);

        Metrics metrics_srtf = {0};
        simulate_srtf(tasks_srtf, num_tasks, &metrics_srtf);
        print_metrics("Standard SRTF", &metrics_srtf, target_util);
        export_csv_results(csv_file, "SRTF", target_util, &metrics_srtf);



        Task tasks_modified[MAX_TASKS];
        memcpy(tasks_modified, tasks, sizeof(Task) * num_tasks);

        Metrics metrics_modified = {0};
        simulate_modified_srtf(tasks_modified, num_tasks, &metrics_modified);
        print_metrics("Modified SRTF", &metrics_modified, target_util);
        export_csv_results(csv_file, "Modified_SRTF", target_util, &metrics_modified);

        //comparison summary
        printf("Deadline Miss Reduction: %.1f%%\n",
               ((double)(metrics_srtf.deadline_misses - metrics_modified.deadline_misses) /
                (metrics_srtf.deadline_misses + 1)) * 100);
        printf("Critical Task Protection: %d -> %d misses (%.1f%% improvement)\n",
               metrics_srtf.critical_deadline_misses,
               metrics_modified.critical_deadline_misses,
               ((double)(metrics_srtf.critical_deadline_misses - metrics_modified.critical_deadline_misses) /
                (metrics_srtf.critical_deadline_misses + 1)) * 100);
        printf("\n");
    }

    fclose(csv_file);
    printf("results saved to: simulation_results.csv\n");

    return 0;
}

//generate task set with given target utilization
void generate_task_set(Task tasks[], int *num_tasks, double utilization) {
    *num_tasks = (int)(10 + utilization * 20);
    if (*num_tasks > MAX_TASKS) *num_tasks = MAX_TASKS;

    //60% critical, 40% non-critical
    int num_critical = (int)(*num_tasks * 0.6);

    double current_util = 0.0;

    for (int i = 0; i < *num_tasks; i++) {
        tasks[i].id = i + 1;

        tasks[i].arrival_time = rand() % 500;

        tasks[i].execution_time = 10 + rand() % 91;
        tasks[i].remaining_time = tasks[i].execution_time;

        tasks[i].priority = (i < num_critical) ? 0 : 1;  // 0 = critical


        if (tasks[i].priority == 0) {
            tasks[i].deadline = tasks[i].arrival_time +
                               tasks[i].execution_time +
                               (rand() % tasks[i].execution_time);
        } else {
            tasks[i].deadline = tasks[i].arrival_time +
                               tasks[i].execution_time * (2 + rand() % 3);
        }

        //initialize other fields
        tasks[i].completion_time = -1;
        tasks[i].start_time = -1;
        tasks[i].response_time = 0;
        tasks[i].turnaround_time = 0;
        tasks[i].waiting_time = 0;
        tasks[i].completed = false;
        tasks[i].deadline_met = false;
        tasks[i].preemption_count = 0;
    }
}

//standard SRTF scheduler
void simulate_srtf(Task tasks[], int num_tasks, Metrics *metrics) {
    int current_time = 0;
    int completed = 0;
    int last_task = -1;

    while (completed < num_tasks && current_time < MAX_TIME) {
        int next_task = find_next_task_srtf(tasks, num_tasks, current_time);

        if (next_task == -1) {
            current_time++;
            continue;
        }

        //track preemptions
        if (last_task != -1 && last_task != next_task && !tasks[last_task].completed) {
            tasks[last_task].preemption_count++;
        }

        //record start time
        if (tasks[next_task].start_time == -1) {
            tasks[next_task].start_time = current_time;
            tasks[next_task].response_time = current_time - tasks[next_task].arrival_time;
        }

        tasks[next_task].remaining_time--;
        current_time++;

        //check if task completed
        if (tasks[next_task].remaining_time == 0) {
            tasks[next_task].completed = true;
            tasks[next_task].completion_time = current_time;
            tasks[next_task].turnaround_time = current_time - tasks[next_task].arrival_time;
            tasks[next_task].waiting_time = tasks[next_task].turnaround_time - tasks[next_task].execution_time;
            tasks[next_task].deadline_met = (current_time <= tasks[next_task].deadline);
            completed++;
        }

        last_task = next_task;
    }

    calculate_metrics(tasks, num_tasks, metrics, current_time);
}

//modified priority-based SRTF scheduler
void simulate_modified_srtf(Task tasks[], int num_tasks, Metrics *metrics) {
    int current_time = 0;
    int completed = 0;
    int last_task = -1;

    while (completed < num_tasks && current_time < MAX_TIME) {
        int next_task = find_next_task_modified(tasks, num_tasks, current_time);

        if (next_task == -1) {
            current_time++;
            continue;
        }

        //track preemptions
        if (last_task != -1 && last_task != next_task && !tasks[last_task].completed) {
            tasks[last_task].preemption_count++;
        }

        //record start time
        if (tasks[next_task].start_time == -1) {
            tasks[next_task].start_time = current_time;
            tasks[next_task].response_time = current_time - tasks[next_task].arrival_time;
        }

        tasks[next_task].remaining_time--;
        current_time++;

        //check if task completed
        if (tasks[next_task].remaining_time == 0) {
            tasks[next_task].completed = true;
            tasks[next_task].completion_time = current_time;
            tasks[next_task].turnaround_time = current_time - tasks[next_task].arrival_time;
            tasks[next_task].waiting_time = tasks[next_task].turnaround_time - tasks[next_task].execution_time;
            tasks[next_task].deadline_met = (current_time <= tasks[next_task].deadline);
            completed++;
        }

        last_task = next_task;
    }

    calculate_metrics(tasks, num_tasks, metrics, current_time);
}

//find next task for standard SRTF
int find_next_task_srtf(Task tasks[], int num_tasks, int current_time) {
    int selected = -1;
    int min_remaining = MAX_TIME + 1;

    for (int i = 0; i < num_tasks; i++) {
        if (!tasks[i].completed &&
            tasks[i].arrival_time <= current_time &&
            tasks[i].remaining_time < min_remaining) {
            min_remaining = tasks[i].remaining_time;
            selected = i;
        }
    }

    return selected;
}

//find next task for modified SRTF
int find_next_task_modified(Task tasks[], int num_tasks, int current_time) {
    int selected = -1;
    int min_remaining = MAX_TIME + 1;
    int best_priority = 2;  //lower is better

    for (int i = 0; i < num_tasks; i++) {
        if (!tasks[i].completed && tasks[i].arrival_time <= current_time) {
            //check priority
            if (tasks[i].priority < best_priority) {
                best_priority = tasks[i].priority;
                min_remaining = tasks[i].remaining_time;
                selected = i;
            }
            //if same priority, check remaining time
            else if (tasks[i].priority == best_priority &&
                     tasks[i].remaining_time < min_remaining) {
                min_remaining = tasks[i].remaining_time;
                selected = i;
            }
        }
    }

    return selected;
}

//calculate metrics
void calculate_metrics(Task tasks[], int num_tasks, Metrics *metrics, int total_time) {
    metrics->total_tasks = num_tasks;
    metrics->completed_tasks = 0;
    metrics->deadline_misses = 0;
    metrics->critical_deadline_misses = 0;
    metrics->noncritical_deadline_misses = 0;
    metrics->total_preemptions = 0;

    double total_response = 0;
    double total_turnaround = 0;
    double total_waiting = 0;
    double total_slack = 0;
    double total_execution = 0;

    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i].completed) {
            metrics->completed_tasks++;
            total_response += tasks[i].response_time;
            total_turnaround += tasks[i].turnaround_time;
            total_waiting += tasks[i].waiting_time;

            //slack time = deadline - completion_time
            int slack = tasks[i].deadline - tasks[i].completion_time;
            total_slack += slack;

            if (!tasks[i].deadline_met) {
                metrics->deadline_misses++;
                if (tasks[i].priority == 0) {
                    metrics->critical_deadline_misses++;
                } else {
                    metrics->noncritical_deadline_misses++;
                }
            }
        }

        total_execution += tasks[i].execution_time;
        metrics->total_preemptions += tasks[i].preemption_count;
    }

    metrics->dropped_jobs = num_tasks - metrics->completed_tasks;

    if (metrics->completed_tasks > 0) {
        metrics->avg_response_time = total_response / metrics->completed_tasks;
        metrics->avg_turnaround_time = total_turnaround / metrics->completed_tasks;
        metrics->avg_waiting_time = total_waiting / metrics->completed_tasks;
        metrics->avg_slack_time = total_slack / metrics->completed_tasks;
    }

    metrics->cpu_utilization = (total_execution / total_time) * 100.0;
}

void print_metrics(const char *scheduler_name, Metrics *metrics, double utilization) {
    printf("Target Utilization:      %.2f\n", utilization);
    printf("Total Tasks:             %d\n", metrics->total_tasks);
    printf("Completed Tasks:         %d\n", metrics->completed_tasks);
    printf("Dropped Jobs:            %d\n", metrics->dropped_jobs);
    printf("Total Deadline Misses:   %d\n", metrics->deadline_misses);
    printf("  - Critical Misses:     %d\n", metrics->critical_deadline_misses);
    printf("  - Non-Critical Misses: %d\n", metrics->noncritical_deadline_misses);
    printf("Avg Response Time:       %.2f\n", metrics->avg_response_time);
    printf("Avg Turnaround Time:     %.2f\n", metrics->avg_turnaround_time);
    printf("Avg Waiting Time:        %.2f\n", metrics->avg_waiting_time);
    printf("Avg Slack Time:          %.2f\n", metrics->avg_slack_time);
    printf("CPU Utilization:         %.2f%%\n", metrics->cpu_utilization);
    printf("Total Preemptions:       %d\n", metrics->total_preemptions);
}

void export_csv_results(FILE *fp, const char *scheduler, double util, Metrics *metrics) {
    fprintf(fp, "%s,%.2f,%d,%d,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%d\n",
            scheduler,
            util,
            metrics->total_tasks,
            metrics->completed_tasks,
            metrics->deadline_misses,
            metrics->critical_deadline_misses,
            metrics->noncritical_deadline_misses,
            metrics->dropped_jobs,
            metrics->avg_response_time,
            metrics->avg_turnaround_time,
            metrics->avg_waiting_time,
            metrics->avg_slack_time,
            metrics->cpu_utilization,
            metrics->total_preemptions);
}