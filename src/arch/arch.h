/*
 * ZenithOS v6.0 - architecture abstraction interface.
 * The kernel calls only this interface; each concrete architecture
 * (src/arch/x86_32, src/arch/arm64) provides the implementation.
 */
#ifndef ZENITH_ARCH_H
#define ZENITH_ARCH_H

void arch_early_init(void);   /* GDT/IDT/interrupt controllers, no drivers */
void arch_halt(void);         /* stop this CPU; never returns                 */

#endif /* ZENITH_ARCH_H */
