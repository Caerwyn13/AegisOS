#include "task.h"
#include "string.h"

#define TASK_MAX 8
#define TASK_STACK_SIZE 8192
#define TASK_TIME_SLICE_TICKS 10

typedef enum {
    TASK_FREE,
    TASK_READY,
    TASK_RUNNING,
    TASK_DEAD
} task_state_t;

typedef struct {
    registers_t *frame;
    task_entry_t entry;
    void *arg;
    task_state_t state;
    uint8_t stack[TASK_STACK_SIZE] __attribute__((aligned(16)));
} task_t;

static task_t tasks[TASK_MAX];
static task_t idle_task;
static task_t *volatile current_task;
static uint32_t last_task_index;
static uint32_t ticks_in_slice;

static void task_start(void);
static void task_exit(void) __attribute__((noreturn));

static uint32_t irq_save(void) {
    uint32_t flags;
    __asm__ volatile ("pushfl; popl %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static void irq_restore(uint32_t flags) {
    if (flags & 0x200)
        __asm__ volatile ("sti" : : : "memory");
}

void task_init(void) {
    memset(tasks, 0, sizeof(tasks));
    memset(&idle_task, 0, sizeof(idle_task));
    idle_task.state = TASK_RUNNING;
    current_task = &idle_task;
    last_task_index = TASK_MAX - 1;
    ticks_in_slice = 0;
}

int task_create(task_entry_t entry, void *arg) {
    if (!entry)
        return -1;

    uint32_t flags = irq_save();
    int index;
    for (index = 0; index < TASK_MAX; index++) {
        if (tasks[index].state == TASK_FREE || tasks[index].state == TASK_DEAD)
            break;
    }

    if (index == TASK_MAX) {
        irq_restore(flags);
        return -1;
    }

    task_t *task = &tasks[index];
    memset(task->stack, 0, sizeof(task->stack));
    task->entry = entry;
    task->arg = arg;
    task->state = TASK_READY;

    registers_t *frame = (registers_t *)(task->stack + TASK_STACK_SIZE -
                                         sizeof(registers_t));
    memset(frame, 0, sizeof(*frame));
    frame->ds = 0x10;
    frame->eip = (uint32_t)task_start;
    frame->cs = 0x08;
    frame->eflags = 0x202;
    frame->useresp = (uint32_t)task_exit;
    task->frame = frame;

    irq_restore(flags);
    return index;
}

void task_yield(void) {
    __asm__ volatile ("int $0x81" : : : "memory");
}

static void task_start(void) {
    task_t *task = current_task;
    task->entry(task->arg);
}

static void task_exit(void) {
    (void)irq_save();
    current_task->state = TASK_DEAD;
    task_yield();
    for (;;)
        __asm__ volatile ("hlt");
}

registers_t *task_schedule(registers_t *regs) {
    int voluntary = regs->int_no == 0x81;

    if (current_task == &idle_task) {
        idle_task.frame = regs;
    } else if (current_task->state == TASK_RUNNING) {
        if (!voluntary && ++ticks_in_slice < TASK_TIME_SLICE_TICKS)
            return regs;

        current_task->frame = regs;
        current_task->state = TASK_READY;
    }

    if (!voluntary && current_task == &idle_task &&
        ++ticks_in_slice < TASK_TIME_SLICE_TICKS)
        return regs;

    ticks_in_slice = 0;

    uint32_t start = (current_task == &idle_task)
                         ? (last_task_index + 1) % TASK_MAX
                         : ((uint32_t)(current_task - tasks) + 1) % TASK_MAX;

    for (uint32_t offset = 0; offset < TASK_MAX; offset++) {
        uint32_t index = (start + offset) % TASK_MAX;
        if (tasks[index].state == TASK_READY) {
            tasks[index].state = TASK_RUNNING;
            current_task = &tasks[index];
            last_task_index = index;
            return tasks[index].frame;
        }
    }

    if (current_task != &idle_task && current_task->state == TASK_READY) {
        current_task->state = TASK_RUNNING;
        return current_task->frame;
    }

    current_task = &idle_task;
    idle_task.state = TASK_RUNNING;
    return idle_task.frame ? idle_task.frame : regs;
}
