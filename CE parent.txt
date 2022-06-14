#include "layout.h"

FILE *logfi;
static const int user_chance = 90;
static int maxchildren = 100;
struct shm_layout *str;
int counter, shm;
static int pids[100];
int msgid, msgid1;
unsigned int bv = 0;
int maxTimeBetweenNewProcsNS = 1000000;
int maxTimeBetweenNewProcsSecs = 2;
int lines = 0;
int stop;

struct stat_object {
    unsigned long s_total;
    unsigned long n_total;
    int id;
};

struct stat_object s_idle, s_processing, s_blocked;
unsigned long s_total;
unsigned long n_total;
    unsigned int tsstart, tnstart;

void bv_delete_pid (unsigned int *bv, unsigned int pid)
{
    (*bv) &= ~(1 << pid);
}

void bv_add_pid (unsigned int *bv, unsigned int pid)
{
    (*bv) |= (1 << pid);
}

int bv_check_pid (unsigned int bv, unsigned int pid)
{
    return (bv & (1 << pid));
}

int bv_get_pid (unsigned int bv)
{
    int i;
    for (i = 0; i < NUM_PCBS; i++)
        if (!bv_check_pid(bv, i))
            return i;
    return -1;
}
int bv_count (unsigned int bv)
{
    int i, total = 0;
    for (i = 0; i < NUM_PCBS; i++)
        if (bv_check_pid(bv, i))
            total++;
    return total;
}

struct node {
    struct stat_object level;
    struct node *next;
};
struct queue {
    struct node *head, *tail;
};
int pop (struct queue *q, struct stat_object *level)
{
    struct node *tmp;
    if (q->head == NULL)
        return -1;
    
    level->s_total = q->head->level.s_total;
    level->n_total = q->head->level.n_total;
    level->id = q->head->level.id;
    tmp = q->head;
    if (q->head == q->tail)
        q->head = q->tail = NULL;
    else
        q->head = q->head->next;
    
    free(tmp);
    return 0;
}
int push (struct queue *q, struct stat_object level)
{
    struct node *nw;
    if (!(nw = (struct node *)calloc(sizeof(struct node), 1))) {
        perror("calloc");
        return -1;
    }
    nw->level.s_total = level.s_total;
    nw->level.n_total = level.n_total;
    nw->level.id = level.id;
    nw->next = NULL;

    if (!q->tail && !q->head) {
        q->tail = q->head = nw;
        return 0;
    }
    q->tail->next = nw;
    q->tail = nw;
    
    return 0;
}
int qcount (struct queue *q)
{
    struct node *ptr;
    int c = 0;
    if (!q || !q->head)
        return -1;
    for (ptr = q->head; ptr; ptr = ptr->next)
        ++c;
    return c;
}

void handler1 (int sig)
{
    fprintf(stderr, "interrupt %d received\n", sig);
    stop = 1;
    int i;
    for (i = 0; i < maxchildren; i++) {
        if (pids[i] > 0) {
            kill(pids[i], SIGCHLD);
        }
        pids[i] = 0;
    }
    wait(NULL);
    
    msgctl(msgid, IPC_RMID, NULL);
    msgctl(msgid1, IPC_RMID, NULL);
    fclose(logfi);
    
    usleep(50000);
    
    shmdt(str);
    shmctl(shm, IPC_RMID, NULL);
    str = 0;
}

void handler2 (int sig)
{
    int i, j;
    //fprintf(stderr, "got %d (%d) bv %u\n", sig, counter, bv);
    wait(NULL);
    if (!str)
        return;
    for (j = 0; j < NUM_PCBS; j++) {
        if (!str->pcbs[j].done || !str->pcbs[j].realpid)
            continue;
        for (i = 0; i < maxchildren; i++) {
            if (pids[i] == str->pcbs[j].realpid) {
                //fprintf(stderr, "found pid %d (id %d %d)\n", pids[i], i, j);
                pids[i] = 0;
                bv_delete_pid(&bv, j);
                str->pcbs[j].realpid = 0;
                str->pcbs[j].pid = -1;
                str->pcbs[j].done = 0;
                break;
            }
        }
    }
}

void write_log (const char *str)
{
    if (++lines > 10000) {
        fprintf(stderr, "error: too many lines\n");
        return;
    }
        
    fprintf(stderr, "%s", str);
    fprintf(logfi, "%s", str);
}

void age_step (int except)
{
    if (!str)
        return;
    int i;
    for (i = 0; i < NUM_PCBS; i++) {
        if (str->pcbs[i].realpid) {
            //printf("%d ", str->pcbs[i].priority);
            if (i == except)
                str->pcbs[i].priority = 0;
            else
                str->pcbs[i].priority++;
        } else {
            //printf("x ");
            str->pcbs[i].priority = 0;
        }
    }
    //printf("\n");
}

void queue_reorder (struct queue *q)
{
    int c = 0, max = 0;
    struct node *mptr = NULL, *ptr;
    if (!q || !q->head)
        return;
    for (ptr = q->head; ptr; ptr = ptr->next) {
        c++;
        if (str->pcbs[ptr->level.s_total].priority > max) {
            max = str->pcbs[ptr->level.s_total].priority;
            mptr = ptr;
        }
    }
    
    int temp = str->pcbs[q->head->level.s_total].priority;
    
    if (c >= 1 && max > temp) {
        //printf("c %d max %d head %d\n", c, max, temp);
        q->head->level.s_total = max;
        mptr->level.s_total = temp;
    }
}

void handle_blocked (struct queue qs[5])
{
    struct queue qtemp;
    struct stat_object stemp;
    memset(&qtemp, 0, sizeof(qtemp));
    while (pop(&qs[4], &stemp) >= 0) {
        if (stemp.s_total >= str->seconds && 
            stemp.n_total > str->nanoseconds) {
            struct stat_object newl = { stemp.id, 0, stemp.id };
            push(&qs[0], newl);
        } else {
            push(&qtemp, stemp);
        }
    }
    memcpy(&qs[4], &qtemp, sizeof(qtemp));
}

int handle_msgs (int flag, struct queue qs[5])
{
    char buf[256];
    struct message message1;
    if (msgrcv(msgid1, &message1, sizeof(message1), 0, IPC_NOWAIT) < 0 || !bv)
        return flag;
        
    int fid, i1, i2;
    sscanf(message1.text, "%d %d %d", &fid, &i1, &i2);
    int id = (int)(message1.type - 1);
    int term = 0;
    if (fid < 0) {
        term = 1;
        fid = abs(fid);
    }

    if (fid) {
    snprintf(buf, 256, "OSS: Receiving that process with PID %d ran for %d/%d nanoseconds%s%s\n", id, fid, i1, term ? " and terminated" : "", fid == str->pcbs[id].quantum ? "" : " and did not use full quantum");
    write_log(buf);

    snprintf(buf, 256, "OSS: total time this dispatch was %d:%d\n", str->seconds - tsstart, str->nanoseconds - tnstart);
    write_log(buf);
    }
    
    if (!str)
        return flag;

    struct stat_object s = { id, 0, id };
    
    if (!term && fid > 0) {
        if (fid <= BASE_QUANTA * 1)
            push(&qs[1], s);
        else if (fid <= BASE_QUANTA * 2)
            push(&qs[2], s);
        else 
            push(&qs[3], s);
        str->pcbs[id].time_during_last = fid;
    }
    if (!term && fid == 0) {
        struct stat_object s2 = { str->seconds + i1, 
                    str->nanoseconds + i2,
                    id };
        push(&qs[4], s2);
        str->pcbs[id].time_during_last = 0;
    }
    flag = 0;

    return flag;
}

void do_timer (int inc_n, int flag, int block_count)
{
    str->seconds += 1;
    s_total++;
    if (flag) {
        s_processing.s_total++;
    } else {
        s_idle.s_total++;
    }
    if (block_count > 0)
        s_blocked.s_total++;
    if (str->nanoseconds > (unsigned int)(1000000000 - inc_n)) {
        str->seconds++;
        s_total++;
        str->nanoseconds = 0;
        if (flag) {
            s_processing.s_total++;
        } else {
            s_idle.s_total++;
        }
        if (block_count > 0)
            s_blocked.s_total++;
    } else {
        str->nanoseconds += inc_n;
        n_total += inc_n;
        if (flag) {
            s_processing.n_total += inc_n;
        } else {
            s_idle.n_total += inc_n;
        }
        if (block_count > 0)
            s_blocked.n_total += inc_n;
    }        
}

int main (int argc, const char **argv)
{
    int i, next_s, next_ns, flag = 0;
    pid_t pid;
    key_t key, key1, key2;
    char buf[256];
    struct queue qs[5];
    for (i = 0; i < 5; i++)
        memset(&qs[i], 0, sizeof(qs[i]));
        
    alarm(3);
    
    signal(SIGALRM, handler1);
    signal(SIGINT, handler1);
    signal(SIGCHLD, handler2);
        
    key = ftok("oss.c", 123);
    key1 = ftok("child.c", 321);
    key2 = ftok("child.c", 322);
        
    printf("got key %x\n", key);

    if ((shm = shmget(key, sizeof(struct shm_layout), 0666 | IPC_CREAT)) < 0) {
        perror("shmget");
        return -1;
    }
    
    if ((str = (struct shm_layout *)shmat(shm, NULL, 0)) == NULL) {
        perror("shmat");
        return -1;
    }
    
    if ((msgid = msgget(key1, 0666 | IPC_CREAT)) < 0) {
        perror("msgget");
        return -1;
    }
    if ((msgid1 = msgget(key2, 0666 | IPC_CREAT)) < 0) {
        perror("msgget");
        return -1;
    }
    
    logfi = fopen("log.txt", "w");

    memset(str, 0, sizeof(struct shm_layout));
    counter = 0;
    
    next_ns = rand() % maxTimeBetweenNewProcsNS;
    next_s = rand() % maxTimeBetweenNewProcsSecs;
    
    do {
        if (str && str->seconds >= next_s && str->nanoseconds >= next_ns && 
            counter < maxchildren) {
                int nextid = bv_get_pid(bv);
                if (nextid == -1 || !str) {
                    next_ns += rand() % maxTimeBetweenNewProcsNS;
                next_s += rand() % maxTimeBetweenNewProcsSecs;
                } else {
                pid = fork();
                if (pid == -1) {
                    perror("fork");
                    kill(getpid(), SIGINT);
                    wait(NULL);
                    shmdt(str);
                    shmctl(shm, IPC_RMID, NULL);
                    str = 0;
                    return -1;
                }
                if (pid != 0) {
                    if (!str)
                        break;
                    bv_add_pid(&bv, nextid);
                    
                    snprintf(buf, 256, "OSS: Generating process with PID %d %d and putting it in queue 0 at time %d:%d\n", 
                        nextid, pid, str->seconds, str->nanoseconds);
                    write_log(buf);
                    next_ns += rand() % maxTimeBetweenNewProcsNS;
                    next_s += rand() % maxTimeBetweenNewProcsSecs;
                    pids[counter] = pid;
                    counter++;
                    str->pcbs[nextid].done = 0;
                    str->pcbs[nextid].realpid = pid;
                    str->pcbs[nextid].total_cpu_time = 0;
                    str->pcbs[nextid].total_sys_time = 0;
                    str->pcbs[nextid].time_during_last = 0;
                    str->pcbs[nextid].pid = nextid;
                    str->pcbs[nextid].type = ((rand() % 100) < user_chance) ? USER : REALTIME;
                    str->pcbs[nextid].priority = 0;
                    struct stat_object s = { nextid, 0, nextid };
                    push(&qs[0], s);
                } else if (pid == 0) { // child
                    execl("./child", "./child", NULL);
                    perror("execl");
                    return -1;
                }
            }
        }
        
        int inc_n = (rand() % 1001);
        int next_pid = -1;
        
        int block_count = qcount(&qs[4]);
        
        if (!flag) {
            if (block_count > 0) {
                handle_blocked(qs);
                inc_n += ((rand() % (10001 - 100)) + 100);
            }
            for (i = 0; i < 4; i++) {
                if (!str)
                    break;
                if (qcount(&qs[i]) <= 0)
                    continue;
                queue_reorder(&qs[i]);
                struct stat_object s;
            
                if (pop(&qs[i], &s) < 0) {
                    perror("pop");
                    return -1;
                }
                next_pid = s.s_total;
                str->pcbs[next_pid].quantum = BASE_QUANTA * (i + 1);
                snprintf(buf, 256, "OSS: Dispatching process with pid %d from queue %d at time %d.%d bv %u\n", next_pid, i, str->seconds, str->nanoseconds, bv);
                write_log(buf);
                char buf2[16];
                snprintf(buf2, 16, "%d", BASE_QUANTA * (i + 1));
                struct message message;
                message.type = next_pid + 1;
                strncpy(message.text, buf2, 16);
                msgsnd(msgid, &message, sizeof(message), 0);
                tsstart = str->seconds;
                tnstart = str->nanoseconds;
                inc_n += ((rand() % (10001 - 100)) + 100);
                flag = 1;
                age_step(next_pid);
                break;
            }
        }
        if (!str)
            break;
        
        do_timer(inc_n, flag, block_count);

        flag = handle_msgs(flag, qs);
    } while (!stop && str && (counter < maxchildren || bv));
        
    wait(NULL);
    
    double sdenom = (double)(s_total ? s_total : 1);
    double ndenom = (double)(n_total ? n_total : 1);
    
    double sp_idle = (double)s_idle.s_total / sdenom;
    double sn_idle = (double)s_idle.n_total / ndenom;
    
    double sp_proc = (double)s_processing.s_total / sdenom;
    double sn_proc = (double)s_processing.n_total / ndenom;
    
    double sp_block = (double)s_blocked.s_total / sdenom;
    double sn_block = (double)s_blocked.n_total / ndenom;
    
    fprintf(stderr, "idle: %lu:%lu = %5.2f%% %5.2f%%\n", s_idle.s_total, s_idle.n_total, 
        sp_idle * 100.f, sn_idle * 100.f);
    fprintf(stderr, "proc: %lu:%lu = %5.2f%% %5.2f%%\n", s_processing.s_total,
         s_processing.n_total, sp_proc * 100.f, sn_proc * 100.f); 
    fprintf(stderr, "blocked: %lu:%lu = %5.2f%% %5.2f%%\n", s_blocked.s_total,
         s_blocked.n_total, sp_block * 100.f, sn_block * 100.f);    
    shmdt(str);
    shmctl(shm, IPC_RMID, NULL);
    str = 0;

    msgctl(msgid, IPC_RMID, NULL);
    msgctl(msgid1, IPC_RMID, NULL);
    fclose(logfi);
    
    printf("freed memory\n");
    return 0;
}