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

#include "gloop/base/googleinit.h"

#include <stdio.h>

#include <cstdint>
#include <string>
#include <thread>
#include <vector>

#include "absl/base/macros.h"
#include "absl/flags/flag.h"
#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/strings/str_format.h"
#include "gloop/base/init_google.h"
#include "gloop/base/log_file_flags.h"

// ========================================================================= //

static std::vector<std::string> lex_init_order;

REGISTER_GOOGLE_INITIALIZER(lex_order, C, { lex_init_order.push_back("C"); });

REGISTER_GOOGLE_INITIALIZER(lex_order, A, { lex_init_order.push_back("A"); });

REGISTER_GOOGLE_INITIALIZER(lex_order, A0, { lex_init_order.push_back("A0"); });

REGISTER_GOOGLE_INITIALIZER(lex_order, B, { lex_init_order.push_back("B"); });

REGISTER_GOOGLE_INITIALIZER(lex_order, Xc, { lex_init_order.push_back("Xc"); });
REGISTER_GOOGLE_INITIALIZER_SEQUENCE(lex_order, Xc, A);

REGISTER_GOOGLE_INITIALIZER(lex_order, Xa, { lex_init_order.push_back("Xa"); });

REGISTER_GOOGLE_INITIALIZER(lex_order, Xb, { lex_init_order.push_back("Xb"); });

// tests that using several REGISTER_GOOGLE_INITIALIZER_SEQUENCE
// in one macro works:
#define REGISTER_2_INITIALIZER_SEQUENCES_(type, name1, name2, name) \
  REGISTER_GOOGLE_INITIALIZER_SEQUENCE(type, name1, name);          \
  REGISTER_GOOGLE_INITIALIZER_SEQUENCE(type, name2, name)

REGISTER_2_INITIALIZER_SEQUENCES_(lex_order, Xa, Xb, A);

// ========================================================================= //

static std::vector<std::string> init_order;

// ========================================================================= //

REGISTER_MODULE_INITIALIZER(D, { init_order.push_back("D"); });
DECLARE_MODULE_INITIALIZER(B);
REGISTER_MODULE_INITIALIZER_SEQUENCE(D, B);

REGISTER_MODULE_INITIALIZER(B, {
  init_order.push_back("B.1");
  REQUIRE_MODULE_INITIALIZED(A);
  init_order.push_back("B.2");
  REQUIRE_MODULE_INITIALIZED(D);
  init_order.push_back("B.3");
});

REGISTER_MODULE_INITIALIZER(A, {
  init_order.push_back("A.1");
  REQUIRE_MODULE_INITIALIZED(C);
  init_order.push_back("A.2");
});

REGISTER_MODULE_INITIALIZER(C, { init_order.push_back("C"); });

// ========================================================================= //

// factory-like test case:
// 0_U: user, 1_R, 2_R: registerers, F, FI: factory and factory init
// (prefixed numbers are to make default lex order the reverse of what we want)

// factory's .h:
DECLARE_MODULE_INITIALIZER(F);

// user's .cc:
REGISTER_MODULE_INITIALIZER(0_U, { init_order.push_back("0_U"); });
REGISTER_MODULE_INITIALIZER_SEQUENCE(F, 0_U);

// factory's .h:
DECLARE_MODULE_INITIALIZER(F);
DECLARE_MODULE_INITIALIZER(FI);

// factory impl's .cc:
REGISTER_MODULE_INITIALIZER(1_R, { init_order.push_back("1_R"); });
REGISTER_MODULE_INITIALIZER(2_R, { init_order.push_back("2_R"); });
REGISTER_MODULE_INITIALIZER_SEQUENCE_3(FI, 1_R, F);
REGISTER_MODULE_INITIALIZER_SEQUENCE_3(FI, 2_R, F);

// factory's .cc:
REGISTER_MODULE_INITIALIZER(FI, { init_order.push_back("FI"); });
REGISTER_MODULE_INITIALIZER(F, { init_order.push_back("F"); });
REGISTER_MODULE_INITIALIZER_SEQUENCE(FI, F);

// ========================================================================= //

// interaction with flags parsing:
ABSL_FLAG(int32_t, g_init_check_1, 0, "for the unittest");
ABSL_FLAG(int32_t, g_init_check_2, 0, "for the unittest");

REGISTER_MODULE_INITIALIZER(use_flag, {
  init_order.push_back(absl::StrFormat("use_flag:%d,%d",
                                       absl::GetFlag(FLAGS_g_init_check_1),
                                       absl::GetFlag(FLAGS_g_init_check_2)));
});

REGISTER_MODULE_INITIALIZER(xset_flag, {
  init_order.push_back(absl::StrFormat("set_flag:%d,%d",
                                       absl::GetFlag(FLAGS_g_init_check_1),
                                       absl::GetFlag(FLAGS_g_init_check_2)));
  absl::SetFlag(&FLAGS_g_init_check_1, 1);
  absl::SetFlag(&FLAGS_g_init_check_2, 2);
});

REGISTER_MODULE_INITIALIZER_SEQUENCE(xset_flag, command_line_flags_parsing);
// ========================================================================= //

static std::vector<std::string> thread_order;

#define REGISTER_INIT_DEP(type1, name1, type2, name2) \
  REGISTER_GOOGLE_INITIALIZER(type1, name1, {         \
    std::this_thread::yield();                        \
    thread_order.push_back(#type1 #name1 "{");        \
    std::this_thread::yield();                        \
    REQUIRE_GOOGLE_INITIALIZED(type2, name2);         \
    std::this_thread::yield();                        \
    thread_order.push_back(#type1 #name1 "}");        \
    std::this_thread::yield();                        \
  });

REGISTER_GOOGLE_INITIALIZER(T0, A, {
  std::this_thread::yield();
  thread_order.push_back("T0A");
});

// These also test that one can call REQUIRE_GOOGLE_INITIALIZED
// for one type from initializer for another:
REGISTER_INIT_DEP(T1, A, T0, A)
REGISTER_INIT_DEP(T1, B, T1, C)
REGISTER_INIT_DEP(T1, C, T0, A)
REGISTER_INIT_DEP(T1, D, T2, A)
REGISTER_INIT_DEP(T1, E, T0, A)

REGISTER_INIT_DEP(T2, A, T2, B)
REGISTER_INIT_DEP(T2, B, T1, E)
REGISTER_INIT_DEP(T2, C, T2, D)
REGISTER_INIT_DEP(T2, D, T2, E)
REGISTER_INIT_DEP(T2, E, T0, A)

static void T1Init(int i) {
  std::this_thread::yield();
  if (i % 2) {
    RUN_GOOGLE_INITIALIZERS(T1);
  } else {
    REQUIRE_GOOGLE_INITIALIZED(T1, A);
  }
}
static void T2Init(int i) {
  std::this_thread::yield();
  if (i % 2) {
    REQUIRE_GOOGLE_INITIALIZED(T2, C);
  } else {
    RUN_GOOGLE_INITIALIZERS(T2);
  }
}

// Call RUN_GOOGLE_INITIALIZERS and REQUIRE_GOOGLE_INITIALIZED
// from many threads at once.
void RunInThreads(void (*body)(int i)) {
  std::vector<std::thread> threads;
  for (int i = 0; i < 50; ++i) {
    threads.emplace_back(absl::bind_front(body, i));
  }
  for (std::thread& thread : threads) {
    thread.join();
  }
}

void RunThreadTest() {
  RunInThreads(T1Init);
  RunInThreads(T2Init);

  // clang-format off
  const char* golden_thread_order[] = {
    "T1A{", "T0A", "T1A}",
    "T1B{",
       "T1C{", "T1C}",
    "T1B}",
    "T1D{",
       "T2A{",
         "T2B{",
            "T1E{", "T1E}",
         "T2B}",
       "T2A}",
    "T1D}",
    "T2C{",
      "T2D{",
         "T2E{", "T2E}",
      "T2D}",
    "T2C}",
  };
  // clang-format on

  CHECK_EQ(thread_order.size(), ABSL_ARRAYSIZE(golden_thread_order));
  for (size_t i = 0; i < thread_order.size(); ++i) {
    CHECK_EQ(thread_order[i], golden_thread_order[i]);
  }
}

// ========================================================================= //

int main(int argc, char** argv) {
  absl::SetFlag(&FLAGS_logtostderr, true);

  std::vector<char*> my_argv(argv, argv + argc);
  char arg[] = "--g_init_check_2=2";
  my_argv.push_back(arg);
  argc += 1;
  argv = &(my_argv[0]);
  InitGoogle(argv[0], &argc, &argv, true);

  // clang-format off
  const char* golden_order[] = {
    "set_flag:0,0",
    "FI", "1_R", "2_R", "F", "0_U",
    "A.1", "C", "A.2", "D", "B.1", "B.2", "B.3",
    "use_flag:1,2",
  };
  // clang-format on

  CHECK_EQ(init_order.size(), ABSL_ARRAYSIZE(golden_order));
  for (size_t i = 0; i < init_order.size(); ++i) {
    CHECK_EQ(init_order[i], golden_order[i]);
  }

  // test implicit lexicographic order:

  RUN_GOOGLE_INITIALIZERS(lex_order);

  // clang-format off
  const char* golden_lex_order[] = {
    "Xa", "Xb", "Xc",
    "A", "A0", "B", "C",
  };
  // clang-format on

  CHECK_EQ(lex_init_order.size(), ABSL_ARRAYSIZE(golden_lex_order));
  for (size_t i = 0; i < lex_init_order.size(); ++i) {
    CHECK_EQ(lex_init_order[i], golden_lex_order[i]);
  }

  RunThreadTest();

  fprintf(stdout, "PASS\n");
  return 0;
}
