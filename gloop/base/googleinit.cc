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

//------------------------------------------------------------------------

// Some background justifying extensive reliance on the services of googleinit.
//
// First here's a quick synopsis of the different stages at
// which global/static objects can be initialized in C++ code:
//
// 1. Linker.
//    The object is initialized even before the program loads.
//    * This applies to all global variables of primitive types
//      that are initialized with compile-time constants.
//    * This can be made to apply to classes, but one has to ensure
//      that the class constructor does not do anything and class methods
//      can work fine on top of the underlying bytes
//      that are originally 0-initialized by the linker.
//      Mutex and SpinLock constructed with base::LINKER_INITIALIZED are
//      examples of this.
//    * I.e. this is done only very carefully for classes
//      that absolutely must use this.
// 2. Normal global object construction (GOC for short).
//    * Applies to global/static objects that rely on non-trivial constructors
//      or non-const initializer expressions.
//    * The object initializer is executed during global construction time
//      in a non-well-defined order with respect to other compilation units.
//      (The ordering is consistent within every compilation unit
//       but is linker- and linker-argument-dependent across units.)
//    * Due to the ill-defined cross-module initialization order
//      while allowing non-trivial initialization logic,
//      use of this initialization stage is forbidden by Google style guide.
// 3. Google/module initializers provided by this library.
//    * Object initialization code is given by using provided
//      REGISTER_*_INITIALIZER macros.
//    * Object initialization is triggered via provided RUN_*_INITIALIZERS and
//      REQUIRE_*_INITIALIZED(name) macro calls.
//    * For the initializers of this standard 'module' type this happens
//      automatically during InitGoogle() that is normally called from main().
//    * The ordering of initializer execution
//      can be accurately controlled either
//      a) via REQUIRE_*_INITIALIZED(name) calls
//         e.g. within other initializers (the call makes sure initializer for
//         the named module and all its dependencies have been executed)
//         or
//      b) via REGISTER_*_INITIALIZER_SEQUENCE(name_before, name_after)
//         directives in the .cc files.
//    Note that googleinit is implemented using both of the other two
//    initialization stages in a careful manner so as to function correctly
//    independently of the execution order of global object constructors.
//
// With the linker-time initialization being limited to compile-time constants
// and normal global object construction generally outlawed by the style guide,
// it's clear that something else must be used to ensure non-trivial yet safe
// initialization of global data.
// Main alternatives to the method taken by this library are
//
// A. On-demand initialization.
//    * In this case a module exports methods that do internal locking
//      and initialize themselves on demand on the first use.
//      If done carefully the whole process can be made to work
//      even during global object construction time (which is still tricky
//      and greatly frowned upon by the style guide).
//    * This breaks down if a module needs dependencies
//      that use less robust initialization methods than on-demand.
//    * It also in general can't be applied to cases when we want to
//      link-in and use during program initialization some
//      self-registering implementations.
//      Such self-registration can't be done on demand at any point in time:
//      it's normally implemented as a call from otherwise empty
//      helper global object constructor.
//    * Services provided by googleinit do not suffer any of this drawbacks.
// B. Forced GOC ordering.
//    * As an ugly but portable workaround one can insert a special
//      static variable with constructor into a header file.
//      Thus, relying on the GOC order within every compilation unit
//      one can ensure that the constructor for one of those special
//      static variables is executed before any GOC in any .cc file
//      that includes that header.
//      Combining this with on-demand initialization code and
//      linker-time initialized data one can implement well-defined ordering
//      of GOC-time initialization.
//    * In practice this is too complex and fragile to be
//      the main mechanism of ordering global data initializers.
//    * Same as on-demand initialization alone it can't support having
//      order dependencies on self-registering linked-in libraries.
// C. GOC-time registration of initialization code using
//    on-demand construction and linker-initialized data
//    with later on-demand or fixed-time initialization.
//    * This is the way googleinit is implemented and thus
//      does not need to be used directly by other modules
//      (other than maybe a few low-level exceptions):
//      services provided by googleinit encapsulate this complexity.

//------------------------------------------------------------------------

#include "gloop/base/googleinit.h"

#include <stdint.h>

#ifndef _WIN32
#include <pthread.h>
#endif  // _WIN32

#include <map>
#include <set>
#include <type_traits>
#include <utility>

#include "absl/base/attributes.h"
#include "absl/base/const_init.h"
#include "absl/base/thread_annotations.h"
#include "absl/container/btree_set.h"
#include "absl/flags/flag.h"
#include "absl/log/check.h"
#include "absl/log/internal/globals.h"
#include "absl/log/log.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gloop/base/init_google_flags.h"
#include "gloop/base/port.h"  // IWYU pragma: keep

namespace {
#ifdef _WIN32
using ThreadType = DWORD;
bool ThreadsEqual(ThreadType a, ThreadType b) { return a == b; }
ThreadType ThreadSelf() { return GetCurrentThreadId(); }
#else
using ThreadType = ::pthread_t;
bool ThreadsEqual(ThreadType a, ThreadType b) { return pthread_equal(a, b); }
ThreadType ThreadSelf() { return pthread_self(); }
#endif
}  // namespace

//------------------------------------------------------------------------
// Module initialization support
//------------------------------------------------------------------------

// Lock that protects all module state. Not held while running any initializers.
ABSL_CONST_INIT static absl::Mutex table_lock(absl::kConstInit);

// Data about a GoogleInitializer that we keep.
struct GoogleInitializer::InitializerData {
  // The underlying initializer object.
  GoogleInitializer* initializer_obj;

  // Set of initializer names to be executed before this one.
  // Gets grown via DependencyRegisterer.
  //
  // All string_views inserted into this set are created from string literals,
  // and will remain valid for the life of the program.
  // NOLINTNEXTLINE(google3-runtime-rename-unnecessary-ordering)
  absl::btree_set<absl::string_view> dependency_names;

  // True iff constructor for *initializer_obj has executed.
  // Both GoogleInitializer and DependencyRegisterer global object c-tors
  // result in creation of an InitializerData record
  // for the GoogleInitializer(s) they are mentioning.
  // In the case such an DependencyRegisterer global object c-tor
  // runs before the GoogleInitializer's own c-tor
  // this member will be false till the global object c-tor of the
  // pointed initializer_obj GoogleInitializer itself eventually runs.
  bool initializer_obj_constructed;

  InitializerData()
      : initializer_obj(nullptr), initializer_obj_constructed(false) {}
  explicit InitializerData(GoogleInitializer* i)
      : initializer_obj(i), initializer_obj_constructed(false) {}
};

// Mapping from module name to initializer data.  All string_views inserted
// into maps of this type come from string literals, and will remain valid for
// the life of the program.
using NameMap = std::map<absl::string_view, GoogleInitializer::InitializerData>;

// All the data about one initializer type that we keep.
// Every field talks about info for the particular initializer type.
class GoogleInitializer::TypeData {
 public:
  TypeData()
      : active_initializer_(nullptr),
        have_run_initializers_(false),
        run_count_(0) {}

  InitializerData* GetInitializerData(const char* type, const char* name,
                                      GoogleInitializer* init)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(table_lock) {
    // This routine must not look at any fields of "*init" since its
    // constructor might not have run yet.
    LOG_IF(ERROR, have_run_initializers_)
        << "Registering initializer '" << name
        << "' too late: some initializers of type '" << type
        << "' have executed";
    InitializerData* idata = &initializer_by_name_[name];
    if (idata->initializer_obj == nullptr) {
      idata->initializer_obj = init;
    } else {
      CHECK_EQ(idata->initializer_obj, init)  // sanity
          << "There is more than one initializer with name '" << name << "'";
    }
    return idata;
  }

  void RunAll() ABSL_EXCLUSIVE_LOCKS_REQUIRED(table_lock) {
    DCHECK(!IsRunning()) << "RunInitializers called recursively";
    BeginRun();
    // Normally initializers execute in lexicographic order of their names:
    for (auto& p : initializer_by_name_) {
      RunIfNecessary(p.second.initializer_obj);
    }
    EndRun();
  }

  void RunOne(GoogleInitializer* init)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(table_lock) {
    // IsRunning() == true is OK: Require() can be called recursively
    BeginRun();
    if (active_initializer_ != nullptr) {
      VLOG(4) << "Requiring     " << init->type_ << ":" << init->name_
              << " from " << init->type_ << ":" << active_initializer_->name_;
    } else {
      VLOG(4) << "Requiring     " << init->type_ << ":" << init->name_;
    }
    RunIfNecessary(init);
    EndRun();
  }

 private:
  // Return true iff the calling thread is running initializers of this type.
  bool IsRunning() const ABSL_SHARED_LOCKS_REQUIRED(table_lock) {
    return run_count_ > 0 && ThreadsEqual(ThreadSelf(), runner_tid_);
  }

  void BeginRun() ABSL_EXCLUSIVE_LOCKS_REQUIRED(table_lock) {
    // Wait until no other thread is running initializers for this type.
    // Note that due to Require() calls etc., this thread might be running
    // initializers already in which case we do not need to wait.
    struct State {
      GoogleInitializer::TypeData* type;
      ThreadType this_tid;

      bool CanRun() ABSL_EXCLUSIVE_LOCKS_REQUIRED(table_lock) {
        // Early on in the process lifetime, it can be unsafe to call
        // pthread methods.  However BeginRun() should only be reachable
        // via RunInitializers(), and that should not happen until we
        // are in main.  So we assume we can use ThreadsEqual() here.
        return type->run_count_ == 0 ||
               ThreadsEqual(this_tid, type->runner_tid_);
      }
    };
    State state;
    state.type = this;
    state.this_tid = ThreadSelf();
    table_lock.Await(absl::Condition(&state, &State::CanRun));

    if (run_count_ == 0) runner_tid_ = ThreadSelf();
    run_count_ += 1;
  }

  void EndRun() ABSL_EXCLUSIVE_LOCKS_REQUIRED(table_lock) {
    DCHECK_GT(run_count_, 0);
    DCHECK(ThreadsEqual(runner_tid_, ThreadSelf()));
    run_count_ -= 1;
  }

  // Implementation helper for Require() and RunInitializers:
  // Runs initializer and all its dependencies if that has not happened yet.
  void RunIfNecessary(GoogleInitializer*)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(table_lock);

  // Initializer name to InitializerData map
  // for all the initializers of this type.
  NameMap initializer_by_name_ ABSL_GUARDED_BY(table_lock);

  // Currently active initializer of this type (nullptr if none).
  GoogleInitializer* active_initializer_ ABSL_GUARDED_BY(table_lock);

  // Status flag to sanity-check that all registrations are done
  // before execution of the initializers.
  // True iff have run some initializers of this type.
  bool have_run_initializers_ ABSL_GUARDED_BY(table_lock);

  // If non-zero the number of times the thread identified by runner_tid_
  // has called BeginRun() without a matching EndRun().
  int run_count_ ABSL_GUARDED_BY(table_lock);

  // If run_count_ is non-zero, the identity of the thread that owns
  // these runs.
  ThreadType runner_tid_ ABSL_GUARDED_BY(table_lock);

  ~TypeData();  // never destroyed
  TypeData(const TypeData&) = delete;
  TypeData& operator=(const TypeData&) = delete;
};

// The global initializer table (maps initializer types to TypeData).
typedef std::map<absl::string_view, GoogleInitializer::TypeData*> TypeTable;
static TypeTable* type_table ABSL_GUARDED_BY(table_lock) = nullptr;

// For consistency and greater safety we use locks through-out
// even though GoogleInitializer and DependencyRegisterer c-tors
// are only supposed to be executed by single thread during
// global object construction.

GoogleInitializer::TypeData* GoogleInitializer::InitializerTypeData(
    const char* type) ABSL_EXCLUSIVE_LOCKS_REQUIRED(table_lock) {
  table_lock.AssertHeld();
  if (type_table == nullptr) {  // global on-demand initialization of googleinit
    type_table = new TypeTable;
  }
  TypeTable::iterator type_data = type_table->find(type);
  if (type_data == type_table->end()) {
    // add new record and assign its iterator
    type_data = type_table->insert(std::make_pair(type, new TypeData)).first;
  }
  return type_data->second;
}

GoogleInitializer::GoogleInitializer(base::internal::LiteralTag,
                                     const char* type, const char* name,
                                     Initializer function)
    : type_(type),
      name_(name),
      function_(function),
      done_(false),
      is_active_(false) {
  absl::MutexLock l(table_lock);
  TypeData* type_data = InitializerTypeData(type);
  InitializerData* idata = type_data->GetInitializerData(type, name, this);
  CHECK(!idata->initializer_obj_constructed)
      << ": Multiple occurrences of initializer '" << name << "'";
  idata->initializer_obj_constructed = true;  // we are the c-tor
}

GoogleInitializer::DependencyRegisterer::DependencyRegisterer(
    base::internal::LiteralTag, const char* type, const char* name,
    GoogleInitializer* initializer, const Dependency& dependency) {
  absl::MutexLock l(table_lock);
  TypeData* type_data = InitializerTypeData(type);
  InitializerData* idata =
      type_data->GetInitializerData(type, name, initializer);

  absl::btree_set<absl::string_view>& dependency_names =
      idata->dependency_names;
  if (dependency_names.count(dependency.name)) {
    LOG(ERROR) << "Repeated dependency declaration to run '" << dependency.name
               << "' before '" << name << "'";
  } else {
    dependency_names.insert(dependency.name);
  }

  // Make sure we have a record for 'dependency' in initializer_by_name_:
  type_data->GetInitializerData(type, dependency.name, dependency.initializer);
}

void GoogleInitializer::RunInitializers(base::internal::LiteralTag,
                                        const char* type)
    ABSL_LOCKS_EXCLUDED(table_lock) {
  absl::MutexLock l(table_lock);
  if (type_table == nullptr || type_table->find(type) == type_table->end()) {
    return;  // no initializers registered
  }
  TypeData* type_data = type_table->find(type)->second;
  type_data->RunAll();
}

void GoogleInitializer::Require() ABSL_LOCKS_EXCLUDED(table_lock) {
  absl::MutexLock l(table_lock);
  TypeData* type_data = type_table->find(type_)->second;
  type_data->RunOne(this);
}

// nullptr or set of module initializers currently running; under
// module_running_lock.
static std::set<absl::string_view>* module_running ABSL_GUARDED_BY(table_lock);

// Return set of names of module initializers currently running.
// Used only by InitGoogle(), declared there privately to avoid
// leaking of std::set into all users of base/googleinit.h
void GoogleInitializerGetModuleRunning(std::set<absl::string_view>* running) {
  absl::MutexLock l(table_lock);
  if (module_running == nullptr) {
    running->clear();
  } else {
    *running = *module_running;
  }
}

// Record that module initializer "name" is running.
static void SetModuleRunning(absl::string_view name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(table_lock) {
  if (module_running == nullptr) {
    module_running = new std::set<absl::string_view>;
  }
  module_running->insert(name);
}

// Forget that module initializer "name" is running.
// Requires that SetModuleRunning() has been called previously.
static void ForgetModuleRunning(absl::string_view name)
    ABSL_EXCLUSIVE_LOCKS_REQUIRED(table_lock) {
  module_running->erase(name);
}

void GoogleInitializer::TypeData::RunIfNecessary(GoogleInitializer* init) {
  absl::string_view name = init->name_;
  absl::string_view type = init->type_;
  DCHECK(IsRunning());  // this thread must be running initializers
  CHECK(!init->is_active_) << ": Cycle involving initializer '" << name
                           << "'of type " << type;
  NameMap::const_iterator it = initializer_by_name_.find(name);
  CHECK(it != initializer_by_name_.end() &&
        it->second.initializer_obj_constructed)  // sanity
      << ": Wow! We've managed to attempt to run initializer '" << name
      << "' of type " << type << " before it has been registered via "
      << "its global GoogleInitializer object constructor execution.";
  if (!init->done_) {  // need to run it
    VLOG(4) << "Initializing  " << type << ":" << name;
    init->is_active_ = true;
    absl::Time start = absl::Now();
    {
      GoogleInitializer* last_active_initializer = active_initializer_;
      have_run_initializers_ = true;  // are running this one now
      active_initializer_ = init;
      // Execute dependency_names first
      // (normally in lexicographic order of names):
      for (absl::string_view dep : it->second.dependency_names) {
        VLOG(4) << "Dependency on " << type << ":" << dep << " from " << type
                << ":" << name;
        NameMap::const_iterator dep_init = initializer_by_name_.find(dep);
        CHECK(dep_init != initializer_by_name_.end());  // sanity
        RunIfNecessary(dep_init->second.initializer_obj);
      }
      if (type == "module") {
        SetModuleRunning(name);
      }

      // Unlock during initializer body execution to let it use
      // Require() via REQUIRE_*_INITIALIZED.  Due to
      // type_data->IsRunning() being true, unlocking is safe: no
      // other thread can jump-in until we fully unwind the
      // initializer running recursion.
      table_lock.unlock();
      (*init->function_)();
      table_lock.lock();

      if (type == "module") {
        ForgetModuleRunning(name);
      }
      active_initializer_ = last_active_initializer;
    }
    init->is_active_ = false;
    init->done_ = true;
    const int64_t time_in_ms = absl::ToInt64Milliseconds(absl::Now() - start);
    // VLOG at default visibility any initializer that takes over 100 ms.
    VLOG(time_in_ms > 100 && absl::log_internal::IsInitialized() &&
                 !absl::GetFlag(FLAGS_silent_init)
             ? 0
             : 4)
        << "Finished      " << type << ":" << name << " in " << time_in_ms
        << " ms";
  }
}

static_assert(std::is_trivially_destructible<GoogleInitializer>::value,
              "GoogleInitializers are created in static storage, and must be "
              "trivially destructible.");
static_assert(
    std::is_trivially_destructible<
        GoogleInitializer::DependencyRegisterer>::value,
    "DependencyRegisterers are created in static storage, and must be "
    "trivially destructible.");
