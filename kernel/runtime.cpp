/* Runtime hooks required by the freestanding C++ link. */
extern "C" void __cxa_pure_virtual() { for (;;) asm volatile("cli; hlt"); }
