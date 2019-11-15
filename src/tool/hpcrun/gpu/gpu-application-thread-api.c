//******************************************************************************
// local includes
//******************************************************************************

#include <hpcrun/cct/cct.h>
#include <hpcrun/module-ignore-map.h>
#include <hpcrun/safe-sampling.h>
#include <hpcrun/sample_event.h>

#include "gpu-activity-channel.h"


//******************************************************************************
// macros
//******************************************************************************

#define UNIT_TEST 0

#define DEBUG 0

#if DEBUG
#define PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define PRINT(...)
#endif



//******************************************************************************
// interface operations
//******************************************************************************

void
gpu_application_thread_process_activities
(
 void
)
{
  gpu_activity_channel_consume();
}


cct_node_t *
gpu_application_thread_correlation_callback
(
 uint64_t correlation_id
)
{
  PRINT("enter gpu_correlation_callback %u\n", correlation_id);

  hpcrun_metricVal_t zero_metric_incr = {.i = 0};
  int zero_metric_id = 0; // nothing to see here
  ucontext_t uc;
  getcontext(&uc);

  // NOTE(keren): hpcrun_safe_enter prevent self interruption
  hpcrun_safe_enter();

  cct_node_t *node =
    hpcrun_sample_callpath(&uc, zero_metric_id,
      zero_metric_incr, 0, 1, NULL).sample_node;

  hpcrun_safe_exit();

  // Compress callpath
  // Highest cupti node
  cct_addr_t *node_addr = hpcrun_cct_addr(node);
  static __thread uint16_t libhpcrun_id = 0;

  // The lowest module must be libhpcrun, which is not 0
  // Update libhpcrun_id only the first time
  if (libhpcrun_id == 0) {
    load_module_t* module = hpcrun_loadmap_findById(node_addr->ip_norm.lm_id);
    if (module != NULL && strstr(module->name, "libhpcrun") != NULL) {
      libhpcrun_id = node_addr->ip_norm.lm_id;
    }
  }

  // Skip libhpcrun
  while (libhpcrun_id != 0 && node_addr->ip_norm.lm_id == libhpcrun_id) {
    //hpcrun_cct_delete_self(node);
    node = hpcrun_cct_parent(node);
    node_addr = hpcrun_cct_addr(node);
  }

  // Skip libcupti and libcuda
  while (module_ignore_map_module_id_lookup(node_addr->ip_norm.lm_id)) {
    //hpcrun_cct_delete_self(node);
    node = hpcrun_cct_parent(node);
    node_addr = hpcrun_cct_addr(node);
  }

  return node;
}