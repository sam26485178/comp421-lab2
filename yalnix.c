#include <yalnix.h>
#include <hardware.h>
#include <stdio.h>
// add a comment

int fPF = MEM_INVALID_SIZE/PAGESIZE + 1;
int nPF = MEM_INVALID_SIZE/PAGESIZE + 1;
int numOfFPF = 1;
int pid_count = 0;
//TODO: How to make all invalid null
struct pte initPT[PAGE_TABLE_LEN];
struct pte PTR1[PAGE_TABLE_LEN];
struct pte idlePT[PAGE_TABLE_LEN];
struct pcb inti_pcb;
struct pcb idle_pcb;
//PCB struct
struct pcb {
    int pid;
    SavedContext ctx;
    //Physical address of region 0 pagetable
    void *region0_addr;

}; 
SavedContext *MySwitchFunc(SavedContext *ctxp, void *p1, void *p2); 
void (*func_ptr[TRAP_VECTOR_SIZE])(ExceptionStackFrame *frame) = {0};


void TrapKernel(ExceptionStackFrame *frame);
void TrapClock(ExceptionStackFrame *frame);
void TrapIllegal(ExceptionStackFrame *frame);
void TrapMemory(ExceptionStackFrame *frame);
void TrapMath(ExceptionStackFrame *frame);
void TrapTTYReceive(ExceptionStackFrame *frame);
void TrapTTYTransmit(ExceptionStackFrame *frame);



void KernelStart (ExceptionStackFrame *frame, unsigned int pmem_size, 
        void *orig_brk, char **cmd_args)
{
    int i;

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
        if (i*PAGESIZE < KERNEL_STACK_BASE || i*PAGESIZE >= orig_brk)
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

    for (i = 0;i< (&_etext - VMEM_1_BASE)/PAGESIZE;i++)
    {
        PTR1[i].uprot = PROT_NONE;
        PTR1[i].kprot = (PROT_READ|0|PROT_EXEC);
        PTR1[i].valid = 1;
        PTR1[i].pfn = i+ PAGE_TABLE_LEN;
    }

    for (i;i < (orig_brk - VMEM_1_BASE)/PAGESIZE;i++)
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

    // -------------------------------------------
    // 
    //  Initialize the init process
    // 
    // -------------------------------------------

    //initialize init's PCB's pid
    idle_pcb.pid = pid_count;
    pid_count++;
    init_pcb.region0_addr = initPT;

    //Initialize init's pagetable
    for (i =0; i < KERNEL_STACK_PAGES;i++)
    {
        initPT[PAGE_TABLE_LEN-1-i].uprot = PROT_NONE;
        initPT[PAGE_TABLE_LEN-1-i].kprot = (PROT_READ|PROT_WRITE|0);
        initPT[PAGE_TABLE_LEN-1-i].valid = 1;
        initPT[PAGE_TABLE_LEN-1-i].pfn = VMEM_1_BASE/PAGESIZE-i-1;
    }

    WriteRegister(REG_PTR0,(RCS421RegVal)initPT);
    //TODO: How about saved context?
    


    // -------------------------------------------
    // 
    //  Initialize the idle process
    // 
    // -------------------------------------------


    // Initialize the init's PCB's pid
    inti_pcb.pid =  pid_count;
    pid_count++;
    inti_pcb.region0_addr = (RCS421RegVal)initPT;


}
/**
 * This help function will copy init's stack to idle
 */
SavedContext *MySwitchFunc(SavedContext *ctxp, void *p1, void *p2) {
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
    pte * temp;
    pcb * pcbt;
    int i;
    for (i = 1; i <= KERNAL_STACK_PAGES; i++) {
        memcpy(fPF*PAGESIZE, KERNEL_STACK_LIMIT - i*PAGESIZE, PAGESIZE);//copy memory from init's kernel stack to free physical frame by frame
        pcbt = &(pcb *)p2;// assign pointer pcbt  the  address of pcb of process 2
        temp = (pte *)(PAGE_TABE_LEN-i+pcbt.region0_addr); //temp is the address of the i frame from the kernel_stack_limit
        (*temp).uprot = PROT_NONE;
        (*temp).kprot = (PROT_READ|PROT_WRITE_0);
        (*temp).valid = 1;
        (*temp).pfn = fPF;

        fPF = *(int*)(fPF*PAGESIZE);
        numOfFPF--;
    }

    //Reset current page table of Region0 to pagetable to idle's page table
    WriteRegister(REG_PTR0, (RCS421RegVal)&(p2->region0_addr);
   
    //TODO:Is it necessary to flush here?
    WriteRegister(REG_TLB_FLUSH,TLB_FLUSH_ALL);
    return ctxp;
}

void TrapKernel(ExceptionStackFrame *frame)
{

}

void TrapClock(ExceptionStackFrame *frame)
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
