/******************************************************************************/
/* Important Fall 2015 CSCI 402 usage information:                            */
/*                                                                            */
/* This fils is part of CSCI 402 kernel programming assignments at USC.       */
/* Please understand that you are NOT permitted to distribute or publically   */
/*         display a copy of this file (or ANY PART of it) for any reason.    */
/* If anyone (including your prospective employer) asks you to post the code, */
/*         you must inform them that you do NOT have permissions to do so.    */
/* You are also NOT permitted to remove or alter this comment block.          */
/* If this comment block is removed or altered in a submitted file, 20 points */
/*         will be deducted.                                                  */
/******************************************************************************/

#include "types.h"
#include "globals.h"
#include "kernel.h"

#include "util/gdb.h"
#include "util/init.h"
#include "util/debug.h"
#include "util/string.h"
#include "util/printf.h"

#include "mm/mm.h"
#include "mm/page.h"
#include "mm/pagetable.h"
#include "mm/pframe.h"

#include "vm/vmmap.h"
#include "vm/shadowd.h"
#include "vm/shadow.h"
#include "vm/anon.h"

#include "main/acpi.h"
#include "main/apic.h"
#include "main/interrupt.h"
#include "main/gdt.h"

#include "proc/sched.h"
#include "proc/proc.h"
#include "proc/kthread.h"

#include "drivers/dev.h"
#include "drivers/blockdev.h"
#include "drivers/disk/ata.h"
#include "drivers/tty/virtterm.h"
#include "drivers/pci.h"

#include "api/exec.h"
#include "api/syscall.h"

#include "fs/vfs.h"
#include "fs/vnode.h"
#include "fs/vfs_syscall.h"
#include "fs/fcntl.h"
#include "fs/stat.h"

#include "test/kshell/kshell.h"
#include "errno.h"

 extern void *sunghan_test(int, void*);
    extern void *sunghan_deadlock_test(int, void*);
    extern void *faber_thread_test(int, void*);
    extern int *vfstest_main(int , char **);
extern int faber_fs_thread_test(kshell_t *, int , char **);
extern int faber_directory_test(kshell_t *ksh, int argc, char **argv); 

GDB_DEFINE_HOOK(boot)
GDB_DEFINE_HOOK(initialized)
GDB_DEFINE_HOOK(shutdown)

static void       hard_shutdown(void);
static void      *bootstrap(int arg1, void *arg2);
static void      *idleproc_run(int arg1, void *arg2);
static kthread_t *initproc_create(void);
static void      *initproc_run(int arg1, void *arg2);

static context_t bootstrap_context;
extern int gdb_wait;

/**
 * This is the first real C function ever called. It performs a lot of
 * hardware-specific initialization, then creates a pseudo-context to
 * execute the bootstrap function in.
 */
void
kmain()
{
        GDB_CALL_HOOK(boot);

        dbg_init();
        dbgq(DBG_CORE, "Kernel binary:\n");
        dbgq(DBG_CORE, "  text: 0x%p-0x%p\n", &kernel_start_text, &kernel_end_text);
        dbgq(DBG_CORE, "  data: 0x%p-0x%p\n", &kernel_start_data, &kernel_end_data);
        dbgq(DBG_CORE, "  bss:  0x%p-0x%p\n", &kernel_start_bss, &kernel_end_bss);

        page_init();

        pt_init();
        slab_init();
        pframe_init();

        acpi_init();
        apic_init();
        pci_init();
        intr_init();

        gdt_init();

        /* initialize slab allocators */
#ifdef __VM__
        anon_init();
        shadow_init();
#endif
        vmmap_init();
        proc_init();
        kthread_init();

#ifdef __DRIVERS__
        bytedev_init();
        blockdev_init();
#endif

        void *bstack = page_alloc();
        pagedir_t *bpdir = pt_get();
        KASSERT(NULL != bstack && "Ran out of memory while booting.");
        /* This little loop gives gdb a place to synch up with weenix.  In the
         * past the weenix command started qemu was started with -S which
         * allowed gdb to connect and start before the boot loader ran, but
         * since then a bug has appeared where breakpoints fail if gdb connects
         * before the boot loader runs.  See
         *
         * https://bugs.launchpad.net/qemu/+bug/526653
         *
         * This loop (along with an additional command in init.gdb setting
         * gdb_wait to 0) sticks weenix at a known place so gdb can join a
         * running weenix, set gdb_wait to zero  and catch the breakpoint in
         * bootstrap below.  See Config.mk for how to set GDBWAIT correctly.
         *
         * DANGER: if GDBWAIT != 0, and gdb is not running, this loop will never
         * exit and weenix will not run.  Make SURE the GDBWAIT is set the way
         * you expect.
         */
        /*while (gdb_wait) ;*/
        context_setup(&bootstrap_context, bootstrap, 0, NULL, bstack, PAGE_SIZE, bpdir);
        context_make_active(&bootstrap_context);

        panic("\nReturned to kmain()!!!\n");
}

/**
 * Clears all interrupts and halts, meaning that we will never run
 * again.
 */
static void
hard_shutdown()
{
#ifdef __DRIVERS__
        vt_print_shutdown();
#endif
        __asm__ volatile("cli; hlt");
}

/**
 * This function is called from kmain, however it is not running in a
 * thread context yet. It should create the idle process which will
 * start executing idleproc_run() in a real thread context.  To start
 * executing in the new process's context call context_make_active(),
 * passing in the appropriate context. This function should _NOT_
 * return.
 *
 * Note: Don't forget to set curproc and curthr appropriately.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */
static void *
bootstrap(int arg1, void *arg2)
{

        /* If the next line is removed/altered in your submission, 20 points will be deducted. */
        dbgq(DBG_CORE, "SIGNATURE: 53616c7465645f5f75d4d6807cbe46557c5894883e55a7be357a5954568eccfc0c1d901bcc73a4409c500b4c2ad2554d\n");
        /* necessary to finalize page table information */
        pt_template_init();    
        
        curproc = proc_create("IDLE");
        KASSERT(NULL != curproc);
        dbg(DBG_PRINT," (GRADING1A 1.a) successfully created IDLE process with process id %d\n",curproc->p_pid);
        KASSERT(PID_IDLE == curproc->p_pid);
        dbg(DBG_PRINT," (GRADING1A 1.a) PID_IDLE value is %d and it matches with the idle process id %d\n",PID_IDLE,curproc->p_pid);
        curthr = kthread_create(curproc,idleproc_run,0,NULL);
        KASSERT(NULL != curthr);
        dbg(DBG_PRINT," (GRADING1A 1.a) thread for the idle process has been created successfully!!\n");
        
        context_make_active(&curthr->kt_ctx);

        /*panic("weenix returned to bootstrap()!!! BAD!!!\n");*/
        return NULL;
}

/**
 * Once we're inside of idleproc_run(), we are executing in the context of the
 * first process-- a real context, so we can finally begin running
 * meaningful code.
 *
 * This is the body of process 0. It should initialize all that we didn't
 * already initialize in kmain(), launch the init process (initproc_run),
 * wait for the init process to exit, then halt the machine.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */
static void *
idleproc_run(int arg1, void *arg2)
{
        int status;
        pid_t child;

        /* create init proc */
        kthread_t *initthr = initproc_create();
        init_call_all();
        GDB_CALL_HOOK(initialized);

        /* Create other kernel threads (in order) */

#ifdef __VFS__
        /* Once you have VFS remember to set the current working directory
         * of the idle and init processes */
       /* NOT_YET_IMPLEMENTED("VFS: idleproc_run");*/

        curproc->p_cwd = vfs_root_vn;
        initthr->kt_proc->p_cwd = vfs_root_vn;

        vref(vfs_root_vn);
        /*vref(vfs_root_vn);*/

        /* Here you need to make the null, zero, and tty devices using mknod */
        /* You can't do this until you have VFS, check the include/drivers/dev.h
         * file for macros with the device ID's you will need to pass to mknod */
        /*NOT_YET_IMPLEMENTED("VFS: idleproc_run");*/
        do_mkdir("/dev");
        do_mknod("/dev/null",S_IFCHR,MKDEVID(1,0));
        do_mknod("/dev/zero",S_IFCHR,MKDEVID(1,1));
        do_mknod("/dev/tty0",S_IFCHR,MKDEVID(2,0));
        do_mknod("/dev/tty1",S_IFCHR,MKDEVID(2,1));



#endif

        /* Finally, enable interrupts (we want to make sure interrupts
         * are enabled AFTER all drivers are initialized) */
        intr_enable();

        /* Run initproc */
        sched_make_runnable(initthr);
        /* Now wait for it */
        child = do_waitpid(-1, 0, &status);
        KASSERT(PID_INIT == child);

#ifdef __MTP__
        kthread_reapd_shutdown();
#endif


#ifdef __SHADOWD__
        /* wait for shadowd to shutdown */
        shadowd_shutdown();
#endif

#ifdef __VFS__
        /* Shutdown the vfs: */
        dbg_print("weenix: vfs shutdown...\n");
        vput(curproc->p_cwd);
        if (vfs_shutdown())
                panic("vfs shutdown FAILED!!\n");

#endif

        /* Shutdown the pframe system */
#ifdef __S5FS__
        pframe_shutdown();
#endif

        dbg_print("\nweenix: halted cleanly!\n");
        GDB_CALL_HOOK(shutdown);
        hard_shutdown();
        return NULL;
}

/**
 * This function, called by the idle process (within 'idleproc_run'), creates the
 * process commonly refered to as the "init" process, which should have PID 1.
 *
 * The init process should contain a thread which begins execution in
 * initproc_run().
 *
 * @return a pointer to a newly created thread which will execute
 * initproc_run when it begins executing
 */
static kthread_t *
initproc_create(void)
{
        proc_t *proc_initproc =  proc_create("INIT");
        KASSERT(NULL != proc_initproc);
        dbg(DBG_PRINT," (GRADING1A 1.b) Init process created successfully!\n");
        KASSERT(PID_INIT == proc_initproc->p_pid);
        dbg(DBG_PRINT," (GRADING1A 1.b) PID_INT value is %d and it matches with the newly created init process id %d\n",PID_INIT, proc_initproc->p_pid);
        kthread_t *initthread = kthread_create(proc_initproc,initproc_run,0,NULL);
        KASSERT(initthread != NULL);
        dbg(DBG_PRINT," (GRADING1A 1.b) Thread for init process created successfully\n");
        return initthread;
}

/**
 * The init thread's function changes depending on how far along your Weenix is
 * developed. Before VM/FI, you'll probably just want to have this run whatever
 * tests you've written (possibly in a new process). After VM/FI, you'll just
 * exec "/sbin/init".
 *
 * Both arguments are unused.
 *
 * @param arg1 the first argument (unused)
 * @param arg2 the second argument (unused)
 */

 void vfstest()
 {
     proc_t *vfs = proc_create("vfs test");
             vfs->p_cwd = vfs_root_vn;
            vref(vfs_root_vn);
        kthread_t *test = kthread_create(vfs,(void*)vfstest_main,1,NULL);
        sched_make_runnable(test);
        
        dbg(DBG_PRINT,"initproc_run(): waiting for vfs test process to end \n");
        int s;
        pid_t c;
        c=do_waitpid(-1, 0, &s);
        KASSERT(c);
        vput(vfs_root_vn);
        kthread_cancel(test,(void*)0);
 }

int rename_test(kshell_t *ksh, int argc, char **argv)

 {

const char * oldname = "/oldname";
const char* newnullname = "";
const char* newname = "/newname";

do_mkdir(oldname);

int rename = do_rename(oldname,newnullname);

rename = do_rename(newnullname,newname);

rename = do_rename(oldname,newname);

return rename;
 }
 /* void vfstest2()
 {
     proc_t *vfs = proc_create("vfs test");
             vfs->p_cwd = vfs_root_vn;

        kthread_t *test = kthread_create(vfs,vfstest_main,1,NULL);
        sched_make_runnable(test);
        
        dbg(DBG_PRINT,"initproc_run(): waiting for vfs test process to end \n");
        int s;
        pid_t c;
        c=do_waitpid(-1, 0, &s);
        kthread_cancel(test,(void*)0);
 }


 void sunghanTest()
 {
     proc_t *faber = proc_create("sunghan test");
        kthread_t *test = kthread_create(faber,sunghan_test,0,NULL);
        sched_make_runnable(test);
        
        dbg(DBG_PRINT,"returned\n");
        int s;
        pid_t c;
        c=do_waitpid(-1, 0, &s);
 }

 int fabertest()
 {
    proc_t *faber = proc_create("faber test");
        kthread_t *test = kthread_create(faber,faber_thread_test,0,NULL);
        sched_make_runnable(test);
        dbg(DBG_PRINT,"returned\n");
        int s;
        pid_t c;
        while(do_waitpid(1, 0, &s)!=-ECHILD);
        return 0;

 }

 void deadlock()
{
     proc_t *faber = proc_create("sunghan deadlock test");
        kthread_t *test = kthread_create(faber,sunghan_deadlock_test,0,NULL);
        sched_make_runnable(test);
        
        dbg(DBG_PRINT,"returned\n");
        int s;
        pid_t c;
        c=do_waitpid(-1, 0, &s);
} */

static void *
initproc_run(int arg1, void *arg2)
{


    #ifdef __DRIVERS__

       while(1){
/*
        kshell_add_command("sunghan", sunghanTest, "Runs sunghan test");
        kshell_add_command("faber", fabertest, "Runs Faber test");
        kshell_add_command("deadlock", deadlock, "Runs Faber test");*/

        kshell_add_command("vfstest",(void*)vfstest, "Runs vfs test");
        kshell_add_command("faber",faber_fs_thread_test,"Runs faber test");
        kshell_add_command("faber_dir",faber_directory_test,"Runs faber directory test");
        kshell_add_command("rename_test",rename_test,"Runs rename_test");

           kshell_t *kshell = kshell_create(0);
        if (NULL == kshell) panic("init: Couldn't create kernel shell\n");
        while (kshell_execute_next(kshell));
            kshell_destroy(kshell);
            int s;
        while(do_waitpid(-1, 0, &s)==1);
        return  0;
        }
    #endif /* __DRIVERS__ */
        
}