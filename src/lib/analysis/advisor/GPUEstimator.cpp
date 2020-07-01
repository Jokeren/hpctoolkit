
//************************* System Include Files ****************************

#include <fstream>
#include <iostream>

#include <climits>
#include <cstdio>
#include <cstring>
#include <string>

#include <algorithm>
#include <stack>
#include <typeinfo>
#include <unordered_map>

#include <sys/stat.h>

//*************************** User Include Files ****************************

#include <include/gpu-metric-names.h>
#include <include/uint.h>

#include "GPUArchitecture.hpp"
#include "GPUEstimator.hpp"
#include "GPUOptimizer.hpp"

using std::string;

#include <lib/prof/CCT-Tree.hpp>
#include <lib/prof/Metric-ADesc.hpp>
#include <lib/prof/Metric-Mgr.hpp>
#include <lib/prof/Struct-Tree.hpp>

#include <lib/profxml/PGMReader.hpp>
#include <lib/profxml/XercesUtil.hpp>

#include <lib/prof-lean/hpcrun-metric.h>

#include <lib/binutils/LM.hpp>
#include <lib/binutils/VMAInterval.hpp>

#include <lib/xml/xml.hpp>

#include <lib/support/IOUtil.hpp>
#include <lib/support/Logic.hpp>
#include <lib/support/StrUtil.hpp>
#include <lib/support/diagnostics.h>

#include <iostream>
#include <vector>

#define MIN2(x, y) (x > y ? y : x)

namespace Analysis {

std::pair<double, double>
SequentialGPUEstimator::estimate(double blame,
                                 const KernelStats &kernel_stats) {
  std::pair<double, double> estimate(0.0, 0.0);

  if (blame != 0.0) {
    estimate.first = blame / kernel_stats.total_samples;
    estimate.second =
        kernel_stats.total_samples / (kernel_stats.total_samples - blame);
  }

  return estimate;
}

std::pair<double, double>
SequentialLatencyGPUEstimator::estimate(double blame,
                                        const KernelStats &kernel_stats) {
  std::pair<double, double> estimate(0.0, 0.0);

  if (blame != 0.0) {
    estimate.first = blame / kernel_stats.total_samples;
    estimate.second =
        kernel_stats.total_samples /
        (kernel_stats.total_samples - MIN2(kernel_stats.active_samples, blame));
  }

  return estimate;
}

std::pair<double, double>
ParallelGPUEstimator::estimate(double blame, const KernelStats &kernel_stats) {
  std::pair<double, double> estimate(0.0, 0.0);

  if (blame != 0.0) {
    estimate.first = blame;
    estimate.second = 1 / blame;
  }

  return estimate;
}

std::pair<double, double>
ParallelLatencyGPUEstimator::estimate(double blame,
                                      const KernelStats &kernel_stats) {
  std::pair<double, double> estimate(0.0, 0.0);
  double cur_warps = kernel_stats.active_warps;
  double max_warps = (kernel_stats.threads - 1) / _arch->warp_size() + 1;

  if (cur_warps < _arch->schedulers()) {
    estimate.first = cur_warps / _arch->schedulers();
    estimate.second = _arch->schedulers() / cur_warps;
  } else {
    double issue = blame / static_cast<double>(kernel_stats.total_samples);
    double warp_issue = 1 - pow(1 - issue, cur_warps / _arch->schedulers());
    double new_warp_issue = 1 - pow(1 - issue, max_warps / _arch->schedulers());

    estimate.first = 1 - warp_issue;
    estimate.second = new_warp_issue / warp_issue;
  }

  return estimate;
}

} // namespace Analysis
