namespace cpu {

extern "C" void fastret(Process *p, u64 rax) NORETURN;
extern "C" void slowret(Process *p) NORETURN;
extern "C" void syscall_entry_stub();
extern "C" void syscall_entry_compat();

struct Cpu;
void idle(Cpu *) NORETURN;

void setup_msrs(u64 gs) {
    using x86::seg;
    using x86::rflags;
    using x86::efer;
    using namespace x86::msr;

    wrmsr(STAR, (seg::user_code32_base << 16) | seg::code);
    wrmsr(LSTAR, (uintptr_t)syscall_entry_stub);
    wrmsr(CSTAR, (uintptr_t)syscall_entry_compat);
    // FIXME: We want to clear a lot more flags - Direction for instance.
    // FreeBSD sets PSL_NT|PSL_T|PSL_I|PSL_C|PSL_D
    wrmsr(FMASK, rflags::IF | rflags::VM);
    wrmsr(EFER, rdmsr(EFER) | efer::SCE | efer::NXE);
    wrmsr(GSBASE, gs);
}

struct Cpu {
    Cpu *self;
    u8 *stack;
    Process *process;
    // mem::PerCpu memory
    DList<Process> runqueue;
    Process *irq_process;
    Process *fpu_process;

    // Assume everything else is 0-initialized
    Cpu(): self(this) {
    }

    void start() {
        setup_msrs((u64)this);
    }

    NORETURN void run() {
        if (Process *p = runqueue.pop()) {
            assert(p->is_queued());
            p->unset(proc::Queued);
            switch_to(p);
        } else {
            idle(this);
        }
    }

    void queue(Process *p) {
        log(runqueue, "queue %p. queued=%d flags=%lu\n", p, p->is_queued(), p->flags);
        if (!p->is_queued()) {
            p->set(proc::Queued);
            runqueue.append(p);
        }
    }

    NORETURN void switch_to(Process *p) {
        log(switch, "switch_to %p rip=%#lx fastret=%d queued=%d\n",
                p, p->rip, p->is(proc::FastRet), p->is(proc::Queued));
        p->set(proc::Running);
        process = p;
        // TODO Check fpu_process, etc.
        x86::set_cr3(p->cr3);
        if (p->is(proc::FastRet)) {
            p->unset(proc::FastRet);
            fastret(p, p->regs.rax);
        } else {
            slowret(p);
        }
    }
};

void idle(Cpu *cpu) {
    for (;;) {
        log(idle, "idle\n");
        cpu->process = NULL;
        asm volatile("cli; hlt; cli" ::: "memory");
    }
}

}
