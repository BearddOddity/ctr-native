#ifndef PLATFORM_NATIVE_MEMORY_H
#define PLATFORM_NATIVE_MEMORY_H

#include <macros.h>

void Platform_ConfigureMempackArena(void);
void Platform_RepairResidentPointers(s32 activeMempackIndex);
void *Platform_GetMempackBacking(void);
int Platform_GetMempackBackingSize(void);

// Extended primitive memory (native only; platform/native_memory.c says how it works).
struct PrimMem;
void NativePrimMem_ConfigureFromArgs(int argc, char **argv);
void NativePrimMem_Bind(struct PrimMem *primMem);
u32 NativePrimMem_LinkBytes(const struct PrimMem *primMem);
void NativePrimMem_NoteFrame(const struct PrimMem *primMem);
void NativePrimMem_Report(void);

#endif
