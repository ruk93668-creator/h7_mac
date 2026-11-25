#include "eth_execution.h"
#include <stddef.h>

static EthExecutionCheck_t s_check;
static void *s_context;

int eth_execution_bind(EthExecutionCheck_t check, void *context)
{
    if (check == NULL || s_check != NULL || !check(context))
        return 0;
    s_context = context;
    s_check = check;
    return 1;
}

int eth_execution_allowed(void)
{
#if defined(ETH_NETWORK_SERVICE)
    return s_check != NULL && s_check(s_context);
#else
    return s_check == NULL || s_check(s_context);
#endif
}

#ifdef ETH_NETWORK_SERVICE
#include "arch/cc.h"

void eth_execution_assert(void)
{
    if (!eth_execution_allowed())
        eth_lwip_assert("foreign lwIP execution context", __FILE__, __LINE__);
}
#endif
