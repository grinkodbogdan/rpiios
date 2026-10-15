/* Linux has no Mach host; cctools' NXGetLocalArchInfo falls back on failure. */
unsigned int mach_task_self_ = 0;
unsigned int mach_host_self(void) { return 0; }
int host_info(unsigned int h, int f, void *i, unsigned int *c) { return 5; /* KERN_FAILURE */ }
int mach_port_deallocate(unsigned int t, unsigned int n) { return 0; }
