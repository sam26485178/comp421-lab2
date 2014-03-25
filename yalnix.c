#include <comp421/yalnix.h>

#include <comp421/hardware.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> 
// add a comment

int fPF = MEM_INVALID_SIZE/PAGESIZE + 1;
int nPF = MEM_INVALID_SIZE/PAGESIZE + 1;
int numOfFPF = 1;
int pid_count = 0;
/*
// to track the kernel brk
void *kernel_brk;
int VM_flag = 0;
*/

struct pte initPT[PAGE_TABLE_LEN];
struct pte PTR1[PAGE_TABLE_LEN];
struct pte idlePT[PAGE_TABLE_LEN];
//PCB struct
struct pcb {
    int pid;
    int delayTime; // use for delay kernel call
    SavedContext *ctx;

    //Physical address of region 0 pagetable
    void *region0_addr;
    ExceptionStackFrame *myFrame;
}; 
struct pcb *curPCB;
struct pcb *init_pcb;
struct pcb *idle_pcb;
SavedContext *KernalCopySwitch(SavedContext *ctxp, void *p1, void *p2); 
void (*func_ptr[TRAP_VECTOR_SIZE])(ExceptionStackFrame *frame) = {0};
// add ready queue and delay block queue
typedef struct node
{
    struct pcb *element;
    struct node *pre, *next;
}Node, *NodePtr;

NodePtr ready_queue_head = (NodePtr)malloc(sizeof(Node))   ;
NodePtr delay_block_queue_head = (NodePtr)malloc(sizeof(Node));
NodePtr ready_queue_rear = (NodePtr)malloc(sizeof(Node));
NodePtr delay_block_queue_rear = (NodePtr)malloc(sizeof(Node));



void TrapKernel(ExceptionStackFrame *frame);
void TrapClock(ExceptionStackFrame *frame);
void TrapIllegal(ExceptionStackFrame *frame);
void TrapMemory(ExceptionStackFrame *frame);
void TrapMath(ExceptionStackFrame *frame);
void TrapTTYReceive(ExceptionStackFrame *frame);
void TrapTTYTransmit(ExceptionStackFrame *frame);

int SetKernelBrk(void *addr);

void KernelStart (ExceptionStackFrame *frame, unsigned int pmem_size, 
        void *orig_brk, char **cmd_args)
{
    int i;
    // record kernel brk
    // and need to modify all orig_brk below to kernel_brk
    *kernel_brk = *orig_brk;
    
    // initialize ready queue and delay block queue
    ready_queue_head->pre = NULL;
    ready_queue_head->next = ready_queue_rear;
    ready_queue_rear->pre = ready_queue_head;
    ready_queue_head->next = NULL;
    
    delay_block_queue_head->pre = NULL;
    delay_block_queue_head->next = delay_block_queue_rear;
    delay_block_queue_rear->pre = delay_block_queue_head;
    delay_block_queue_rear->next = NULL;
    
    // -----------------------------------------
    // 
    //    Initialzize the interrupt vector
    //    table entries.
    //      
    // -----------------------------------------
    func_ptr[TRAP_KERNEL] = TrapKernel;
    func_ptr[TRAP_CLOCK] = TrapClock;
    func_ptr[TRAP_ILLEGAL] = TrapIllegal;
    func_ptr[TRAP_MEMORY] = TrapMemory;
    func_ptr[TRAP_MATH] = TrapMath;
    func_ptr[TRAP_TTY_RECEIVE] = TrapTTYReceive;
    func_ptr[TRAP_TTY_TRANSMIT] = TrapTTYTransmit;

    WriteRegister(REG_VECTOR_BASE, (RCS421RegVal)func_ptr);



    // -----------------------------------------
    // 
    //      Build a conceptual linked list of free memory frames. 
    //
    //      The way we implement the linked list:
    //
    //      We write the next free frame number to the first 
    //      address of the current frame.
    //      
    // -----------------------------------------

    // Get the value of kernel heap's original break 
    // (lowest address that is not part of kernal's original heap).
    // For every physical memory frame
    //TODO:Check if the newly added MEM_INVALID_SIZE is correct 
    for (i=MEM_INVALID_SIZE/PAGESIZE + 2;i<pmem_size/PAGESIZE;i++)
    {	//If the frame is above the kernal initialized heap or below the kernal stack
        if (((void *)(unsigned long)(PMEM_BASE  + i*PAGESIZE) < KERNEL_STACK_BASE)|| ((void *)(unsigned long)(PMEM_BASE + i*PAGESIZE)  >= orig_brk))
        {
            //Write i (next nPF)  to the first address of current nPF
            *(int *)(long)(nPF*PAGESIZE) = i;
            //Update the current nPF to i 
            nPF = i;
            //Increment the total number of free frames
            numOfFPF++;
        }	
    }	
    // write -1 to the last free frame 
    *(int *)(long)( nPF*PAGESIZE) = -1;

    // -----------------------------------------
    // 
    //  Initialize the page table for Region 1
    //
    //  1. Initialize text (exec, read)
    //  2. Initialize data/bss/heap (write, read)
    //      
    // -----------------------------------------

    for (i = 0;i< (*&_etext - VMEM_1_BASE)/PAGESIZE;i++)
    {
        PTR1[i].uprot = PROT_NONE;
        PTR1[i].kprot = (PROT_READ|0|PROT_EXEC);
        PTR1[i].valid = 1;
        PTR1[i].pfn = i+ PAGE_TABLE_LEN;
    }

    for (;i < (unsigned long)orig_brk/PAGESIZE;i++)
    {

        PTR1[i].uprot = PROT_NONE;
        PTR1[i].kprot = (PROT_READ|PROT_WRITE|0);
        PTR1[i].valid = 1;
        PTR1[i].pfn = i+ PAGE_TABLE_LEN;
    }	
    //TODO: Is initial page table for Region 0 also for init process?


    WriteRegister(REG_PTR1,(RCS421RegVal)PTR1);

    // -----------
    // 
    //  Enable VM
    //
    // -----------
    //TODO: After enabling VM, do we have to set all the MEM_INVALID_SIZE in page talbe of Region 0 invalid?
    WriteRegister(REG_VM_ENABLE,1);
    
    VM_flag = 1;

    // -------------------------------------------
    // 
    //  Initialize the idle process
    // 
    // -------------------------------------------

    //initialize idle's PCB's pid
    idle_pcb->pid = pid_count;
    pid_count++;

    idle_pcb->region0_addr = idlePT;

    idle_pcb->myFrame = frame;

    //Initialize idle's pagetable
    for (i =0; i < KERNEL_STACK_PAGES;i++)
    {
        idlePT[PAGE_TABLE_LEN-1-i].uprot = PROT_NONE;
        idlePT[PAGE_TABLE_LEN-1-i].kprot = (PROT_READ|PROT_WRITE|0);
        idlePT[PAGE_TABLE_LEN-1-i].valid = 1;
        idlePT[PAGE_TABLE_LEN-1-i].pfn = VMEM_1_BASE - 1 - i;
    }

    WriteRegister(REG_PTR0,(RCS421RegVal)idlePT);
    //TODO: How about saved context?

    LoadProgram(cmd_args[0],cmd_args);


    // -------------------------------------------
    // 
    //  Global variable curPCB point to idle_pcb    
    //
    // -------------------------------------------

    curPCB = idle_pcb;

    // -------------------------------------------
    // 
    //  Initialize the init process
    // 
    // -------------------------------------------


    // Initialize the init's PCB's pid
    init_pcb->pid =  pid_count;
    pid_count++;
    init_pcb->region0_addr = idlePT;


    //TODO: Shall I initialize idle's PT here or in my switch func?
    //Initialize init's pagetable
    for (i =0; i < KERNEL_STACK_PAGES;i++)
    {
        initPT[PAGE_TABLE_LEN-1-i].uprot = PROT_NONE;
        initPT[PAGE_TABLE_LEN-1-i].kprot = (PROT_READ|PROT_WRITE|0);
        initPT[PAGE_TABLE_LEN-1-i].valid = 1;
        initPT[PAGE_TABLE_LEN-1-i].pfn = VMEM_1_BASE - 1 - i;
    }

    // -------------------------------------------
    // 
    //  Copy idle's kernal stack to init using a 
    //  context switch.
    //  
    // -------------------------------------------

    ContextSwitch(KernalCopySwitch,idle_pcb->ctx ,idle_pcb, init_pcb);


    LoadProgram(cmd_args[0],cmd_args);



}
/**
 * This help function will copy idle's stack to init
 */
SavedContext *KernalCopySwitch(SavedContext *ctxp, void *p1, void *p2) {
    // -------------------------------------
    //
    // Copy inti's kernal stack to somewhere
    // 
    //  1. Find KERNAL_STACK_SIZE chunk of free
    //      memory somewhere.
    //
    //  TODO: memcopy?
    //  TODO: Update idle's pagetable here?
    //  2. Copy
    // -------------------------------------
    /* 
    void *p2PT = &(p2->region0_addr);
        p2PT[PAGE_TABLE_LEN-i].uprot = PROT_NONE;
        p2PT[PAGE_TABLE_LEN-i].kprot = (PROT_READ|PROT_WRITE|0);
        p2PT[PAGE_TABLE_LEN-i].valid = 1;
        p2PT[PAGE_TABLE_LEN-i].pfn = fPF;
        
        */
    struct pte * temp;
    struct pcb * pcbt;
    int i;
    for (i = 1; i <= KERNEL_STACK_PAGES; i++) {
        memcpy((void *)(unsigned long)(fPF*PAGESIZE), (void *)(unsigned long)(KERNEL_STACK_LIMIT - i*PAGESIZE), PAGESIZE);//copy memory from init's kernel stack to free physical frame by frame
        //pcbt = &(pcb *)p2;// assign pointer pcbt  the  address of pcb of process 2
        pcbt = (struct pcb *)p2;
        //TODO: how does an integer type add an address (pointer)
        temp = (struct pte *)((PAGE_TABLE_LEN-i)*PAGESIZE+pcbt->region0_addr); //temp is the address of the i frame from the kernel_stack_limit
        (*temp).uprot = PROT_NONE;
        (*temp).kprot = (PROT_READ|PROT_WRITE|0);
        (*temp).valid = 1;
        (*temp).pfn = fPF;

        fPF = *(int*)(unsigned long)(fPF*PAGESIZE);
        numOfFPF--;
    }

    //--------------------------------
    //
    //  TODO:Initialize savedcontext of init
    //
    //  --------------------------------
    
    pcbt->ctx = ctxp;

    //Reset current page table of Region0 to pagetable to idle's page table
    WriteRegister(REG_PTR0, (RCS421RegVal)(pcbt->region0_addr));
   
    //TODO:Is it necessary to flush here?
    WriteRegister(REG_TLB_FLUSH,TLB_FLUSH_ALL);
    return ctxp;
}


int SetKernelBrk(void *addr)
{
    // if VM not enabled, just move kernel_brk to addr
    if (VM_flag == 0) *kernel_brk = *addr;
    else
    {
    // first allocate free memory of size *addr - *kernel_brk from list of free
    // phisical memory
    // second map these new free phisical memory to page_table_1
    // then grow kernel_brk to addr frame by frame
        
        int frameNeeded = (*addr - *kernel_brk)/PAGESIZE;
        for (int i = 0; i < frameNeeded; i++)
        {
            // if run out of phsical memory, return error
            if (numOfFPF == 0) return -1;
            
            int freeFrame = fPF;
            
            // move the head of linked list of free memory to next node
            int *tmp;
            tmp = &(fPF * PAGESIZE);
            fPF = *tmp;
            numOfFPF--;
            
            PTR1[*kernel_brk/PAGESIZE].uprot = PROT_NONE;
            PTR1[*kernel_brk/PAGESIZE].kprot = (PROT_READ|PROT_WRITE|0);
            PTR1[*kernel_brk/PAGESIZE].valid = 1;
            PTR1[*kernel_brk/PAGESIZE].pfn = freeFrame;
            
            *kernel_brk += PAGESIZE;
        }
    }
    return 0;
}
                  
void TrapKernel(ExceptionStackFrame *frame)
{

}

void TrapIllegal(ExceptionStackFrame *frame)
{
}

void TrapMemory(ExceptionStackFrame *frame)
{
}

void TrapMath(ExceptionStackFrame *frame)
{

}

void TrapTTYReceive(ExceptionStackFrame *frame)
{
}

void TrapTTYTransmit(ExceptionStackFrame *frame)
{
}

int Brk(void *addr)
{
    int tmp;
    // invalid address, return 0
    if ( *addr < VMEM_1_BASE || *addr > VMEM_LIMIT) return 0;
    // if addr already in under kernel brk, return 0
    if (*addr <= *kernel_brk) return 0;
    else {
        tmp = SetKernelBrk( addr); // correct?
        return tmp;
    }
    
}
                  
int GetPid(void)
{
    // every process context switch with idle process, so just return idle pid?
    return curPCB ->pid;
}

int Delay(int clock_ticks)
{
    
    // add current process to block queue
    // context switch to the head of ready queue
    // if empty, switch to idle process
    
    // try to find the right place to place current process in block queue
    int tmp = 0;
    int flag = 0;
    NodePtr tmpNode;
    NodePtr p = delay_block_queue_head;
    while ( p->next != delay_block_queue_rear)
    {
        p = p->next;
        tmp += (p.element) ->delayTime;
        if (tmp > clock_ticks)
        {
            tmp -= (p.element) ->delayTime;
            flag = 1;
            break;
        }
    }
    if (p != delay_block_queue_head)
        (p.element) ->delayTime = (p.element) ->delayTime - ( clock_ticks - tmp);
    
    tmpNode = (NodePtr)malloc(sizeof(Node));
    tmpNode.element = curPCB;
    (tmpNode.element) ->delayTime = clock_ticks - tmp;
    if (p == delay_block_queue_head)
    {
        tmpNode ->next = p ->next;
        p ->next ->pre = tmpNode;
        p ->next = tmpNode;
        tmpNode ->pre = p;
    }
    else if (flag == 1)
    {
        tmpNode ->next = p;
        tmpNode ->pre = p ->pre;
        p ->pre->next = tmpNode;
        p->pre = tmpNode;
    }
    else
    {
        tmpNode ->next = p ->next;
        tmpNode ->pre = p;
        p ->next ->pre = tmpNode;
        p ->next = tmpNode;
    }
    
    // context switch current process to the head of ready queue, if null, switch to idle
    NodePtr nextReady = ready_queue_head ->next;

    if ( nextReady != ready_queue_rear)
    {
        ready_queue_head ->next = nextReady ->next;
        nextReady ->next->pre = ready_queue_head;
        ContextSwitch(KernalCopySwitch, &curPCB ->ctx, curPCB, nextReady.element);
        free(nextReady);
    }
    else
        ContextSwitch(KernalCopySwitch, &curPCB ->ctx, curPCB, idle_pcb);
}
                
void TrapClock(ExceptionStackFrame *frame)
{
    // count down delay, if equals to zero, take this blocked process out from
    // the block queue and put to ready queue
    NodePtr p = delay_block_queue_head->next;
    if (p != delay_block_queue_rear) (p.element) ->delayTime--;
    while ((p.element) ->delayTime == 0 && p != delay_block_queue_rear)
    {
        delay_block_queue_head ->next = p->next;
        p->next->pre = delay_block_queue_head;
        
        // add to ready queue
        p->pre = ready_queue_rear->pre;
        ready_queue_rear->pre->next = p;
        p->next = ready_queue_rear;
        ready_queue_rear->pre = p;
        
        p = delay_block_queue_head->next;
    }
}
 
                  
                  
                  
                  
                  
                  
