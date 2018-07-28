#define LOG_NDEBUG 0
#define LOG_TAG "SampleVencMem"

#include <memoryAdapter.h>
#include "sc_interface.h"
#include "plat_log.h"
#include "ion_memmanager.h"

int venc_MemOpen(void)
{
    return ion_memOpen();
}

int venc_MemClose(void)
{
    return ion_memClose();
}

unsigned char* venc_allocMem(unsigned int size)
{
    return ion_allocMem(size);
}

int venc_freeMem(void *vir_ptr)
{
    return ion_freeMem(vir_ptr);
}

unsigned int venc_getPhyAddrByVirAddr(void *vir_ptr)
{
    return ion_getMemPhyAddr(vir_ptr);
}

int venc_flushCache(void *vir_ptr, unsigned int size)
{
    return ion_flushCache(vir_ptr, size);
}
