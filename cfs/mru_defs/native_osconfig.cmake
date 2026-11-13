# OSAL for a normal Linux user, as on OPS-SAT PRETTY (user exp):
# - permissive mode: a queue deeper than the kernel allows a normal user (10
#   messages) is created at the allowed depth, and real-time priorities that
#   need root are skipped, instead of failing;
# - the network layer stays built, because OSAL v7.0.1 does not link without
#   it (OS_NetworkGetID_Impl), but nothing calls it: no application here
#   opens a socket, and scripts/fprime-run4.sh checks that the running
#   process holds none;
# - a static loader only: the app is linked into core-cpu1.
set(OSAL_CONFIG_DEBUG_PERMISSIVE_MODE TRUE)
set(OSAL_CONFIG_DEBUG_PRINTF FALSE)
set(OSAL_CONFIG_UTILITYTASK_PRIORITY 10)
set(OSAL_CONFIG_INCLUDE_DYNAMIC_LOADER FALSE)
set(OSAL_CONFIG_INCLUDE_STATIC_LOADER TRUE)
