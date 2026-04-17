// Copyright 2026 Google LLC.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Removing the following header is prohibited as it can introduce undefined
// behavior.
// clang-format off
#include "gloop/enforce_gloop_support.h"
// clang-format on

#include "gloop/thread/fiber/init-domain.h"

#include "absl/base/attributes.h"
#include "absl/base/config.h"
#include "absl/base/internal/raw_logging.h"
#include "absl/flags/flag.h"
#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"
#include "gloop/base/scheduling/scheduler.h"

#ifdef __APPLE__
#include <sys/sysctl.h>
#endif  // __APPLE__

#ifndef __Fuchsia__
#include <sys/resource.h>
#endif

#include <sys/time.h>

#include <algorithm>
#include <cstdint>
#include <memory>

#include "absl/log/log.h"
#include "gloop/base/sysinfo.h"
#include "gloop/thread/fiber/fifolifo-schedulers.h"
#include "gloop/thread/fiber/futex-domain.h"
#include "gloop/thread/fiber/pthread-domain.h"

ABSL_FLAG(int32_t, fibers_default_domain_concurrency, -1,
          "Default domain concurrency, ~NumCPUs() when unspecified");

// DEPRECATED: Use --fibers_use_futex_domain instead.
ABSL_FLAG(bool, fibers_use_pthread_compatibility_mode, false,
          "Enable to disable the use of SwitchTo.  Should only be used for "
          "debugging.");

ABSL_FLAG(bool, fibers_increase_thread_rlimit, true,
          "Automatically attempt to increase thread rlimit where permitted.");

// Note: this flag is not ready for production.
ABSL_FLAG(bool, fibers_experimental_use_swg_domain, false,
          "Use an SWG Domain as the default domain. "
          "This is an experimental/test flag. Not for prod.");

ABSL_FLAG(bool, fibers_use_futex_domain, false,
          "Use futex domain as the default fiber domain.");

namespace thread {

// Weakly define UMCG Domain symbols until UMCG is ready for prod rollout.
// The functions are "strongly" defined in fiber/internal/umcg-domain.cc.
ABSL_ATTRIBUTE_WEAK bool UseUmcgDomain() { return false; }

// Weakly define UMCG Domain symbols until UMCG is ready for prod rollout.
// The functions are "strongly" defined in fiber/internal/umcg-domain.cc.
ABSL_ATTRIBUTE_WEAK base::scheduling::Domain* NewUmcgDomain(
    absl::string_view name_prefix, int max_concurrency) {
  ABSL_RAW_LOG(FATAL, "UMCG domain unavailable.");
  return nullptr;
}

namespace {

absl::once_flag init_domain_once;
static base::scheduling::Domain* default_domain = nullptr;
static base::scheduling::Scheduler* root_scheduler = nullptr;

#ifdef ABSL_HAVE_THREAD_SANITIZER
int DefaultTsanConcurrency() {
  LOG(INFO) << "Fiber init: running under TSAN, adjusting concurrency";
  constexpr int kMinTsanConcurrency = 4;
  int num_cores = 0;
#ifdef __APPLE__
  size_t len = sizeof(num_cores);
  if (sysctlbyname("hw.logicalcpu_max", &num_cores, &len, nullptr, 0) != 0) {
    PLOG(ERROR) << "Unexpected sysctlbyname() failure";
    num_cores = kMinTsanConcurrency;
  }
#else
  cpu_set_t mask;
  sched_getaffinity(0, sizeof(mask), &mask);
  num_cores = CPU_COUNT(&mask);
#endif  // __APPLE__
  // Here we want to balance between:
  //  (a) Not overloading TSAN, which is known to perform badly when many
  //      active threads share a small number of CPUs.
  //  (b) Not deadlocking user-code that may have assumed higher default domain
  //      concurrency.
  return std::max(num_cores - 2, kMinTsanConcurrency);
}
#endif

void IncreaseThreadLimit() {
  // Fuchsia does not support rlimit.
#ifndef __Fuchsia__
  // If setrlimit is disabled (sandbox, etc), give up.
  struct rlimit lim;
  if (0 != getrlimit(RLIMIT_NPROC, &lim)) {
    PLOG(ERROR) << "Unexpected getrlimit() failure";
    return;
  }
  rlim_t soft = lim.rlim_cur;
  rlim_t hard = lim.rlim_max;
  static const rlim_t kManyThreads = 128 * 1024;
  if (soft == hard || soft >= kManyThreads) {
    // High enough.
    return;
  }
  lim.rlim_cur = std::min(kManyThreads, hard);
  if (0 != setrlimit(RLIMIT_NPROC, &lim)) {
    PLOG(ERROR) << "Unexpected setrlimit() failure";
  }
#endif  // __Fuchsia__
}

std::unique_ptr<base::scheduling::Domain> DoCreateDomain(
    absl::string_view name, int max_concurrency, absl::string_view kind,
    base::scheduling::Domain* (*factory_fn)(absl::string_view, int)) {
  auto prefix = absl::StrCat(kind, "-", name);

  LOG(INFO) << "Fiber init: default domain = " << kind
            << ", concurrency = " << max_concurrency << ", prefix = " << prefix;

  return absl::WrapUnique(factory_fn(prefix, max_concurrency));
}

void InitDefaultDomain() {
  // Tier1 jobs are automatically eligible for increased thread limits. This
  // will also be enabled eventually for tier2 jobs. See http://b/19098236
  if (absl::GetFlag(FLAGS_fibers_increase_thread_rlimit)) {
    IncreaseThreadLimit();
  }

  default_domain = CreateCustomDomain({}).release();
  root_scheduler = NewRootFIFOScheduler(default_domain);
}

}  // namespace

std::unique_ptr<base::scheduling::Domain> CreateCustomDomain(
    const CreateCustomDomainOptions& options) {
  int max_concurrency = options.max_concurrency;
  if (max_concurrency < 0) {
    max_concurrency = absl::GetFlag(FLAGS_fibers_default_domain_concurrency);
  }
  if (max_concurrency < 0) {
#ifndef ABSL_HAVE_THREAD_SANITIZER
    max_concurrency = NumCPUs();
#else
    max_concurrency = DefaultTsanConcurrency();
#endif
    // We (currently) slightly over-schedule to account for unannotated blocking
    // operations.
    max_concurrency += max_concurrency / 10;
  }

  absl::string_view name = options.name;

  if (name.empty()) {
    name = "default";
  }

  // TODO: convert these boolean flags to a single flag.
  int flags_set = 0;
  if (absl::GetFlag(FLAGS_fibers_use_futex_domain)) {
    flags_set += 1;
  }
  if (absl::GetFlag(FLAGS_fibers_experimental_use_swg_domain)) {
    flags_set += 1;
  }
  if (absl::GetFlag(FLAGS_fibers_use_pthread_compatibility_mode)) {
    flags_set += 1;
  }
  if (flags_set > 1) {
    ABSL_RAW_LOG(ERROR, "More than one fiber domain flag set.");
  }

  if (UseUmcgDomain()) {
    return DoCreateDomain(name, max_concurrency, "umcg", &NewUmcgDomain);
  }

  if (FutexDomainAvailable() && absl::GetFlag(FLAGS_fibers_use_futex_domain)) {
    return DoCreateDomain(name, max_concurrency, "futex", &NewFutexDomain);
  }

  if (FutexDomainAvailable() &&
      !absl::GetFlag(FLAGS_fibers_use_pthread_compatibility_mode)) {
    return DoCreateDomain(name, max_concurrency, "futex", &NewFutexDomain);
  }

  return DoCreateDomain(name, max_concurrency, "pthread", &NewPthreadDomain);
}

base::scheduling::Domain* DefaultDomain() {
  absl::call_once(init_domain_once, InitDefaultDomain);
  return default_domain;
}

}  // namespace thread
