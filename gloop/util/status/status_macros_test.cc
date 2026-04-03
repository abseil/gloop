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

#include "gloop/util/status/status_macros.h"

#include <cerrno>
#include <cstddef>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/optimization.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/string_view.h"
#include "absl/types/source_location.h"
#include "benchmark/benchmark.h"
#include "gloop/util/status/posixerrorspace.h"
#include "gloop/util/status/status.h"
#include "gloop/util/status/status_builder.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace {

using ::testing::AllOf;
using ::testing::Eq;
using ::testing::HasSubstr;

absl::Status ReturnOk() { return absl::OkStatus(); }

util::StatusBuilder ReturnOkBuilder() {
  return util::StatusBuilder(absl::OkStatus());
}

absl::Status ReturnError(absl::string_view msg) {
  return absl::Status(absl::StatusCode::kUnknown, msg);
}

util::StatusBuilder ReturnErrorBuilder(absl::string_view msg) {
  return util::StatusBuilder(absl::Status(absl::StatusCode::kUnknown, msg));
}

absl::StatusOr<int> ReturnStatusOrValue(int v) { return v; }

absl::StatusOr<int> ReturnStatusOrError(absl::string_view msg) {
  return absl::Status(absl::StatusCode::kUnknown, msg);
}

template <class... Args>
absl::StatusOr<std::tuple<Args...>> ReturnStatusOrTupleValue(Args&&... v) {
  return std::tuple<Args...>(std::forward<Args>(v)...);
}

template <class... Args>
absl::StatusOr<std::tuple<Args...>> ReturnStatusOrTupleError(
    absl::string_view msg) {
  return absl::Status(absl::StatusCode::kUnknown, msg);
}

absl::StatusOr<int&> ReturnStatusOrRef(int& v) { return v; }

absl::StatusOr<std::unique_ptr<int>> ReturnStatusOrPtrValue(int v) {
  return std::make_unique<int>(v);
}
void CheckSourceLocation(
    const absl::Status& status, std::vector<int> lines = {},
    absl::SourceLocation loc = absl::SourceLocation::current()) {
  ASSERT_EQ(status.GetSourceLocations().size(), lines.size())
      << "Size check failed at " << loc.line();
  for (int i = 0; i < lines.size(); ++i) {
    EXPECT_EQ(absl::string_view(status.GetSourceLocations()[i].file_name()),
              absl::string_view(loc.file_name()))
        << "File name check failed at " << loc.line();
    EXPECT_EQ(status.GetSourceLocations()[i].line(), lines[i])
        << "Line check failed at " << loc.line();
  }
}
TEST(AssignOrReturn, Works) {
  auto func = []() -> absl::Status {
    ASSIGN_OR_RETURN(int value1, ReturnStatusOrValue(1));
    EXPECT_EQ(1, value1);
    ASSIGN_OR_RETURN(const int value2, ReturnStatusOrValue(2));
    EXPECT_EQ(2, value2);
    ASSIGN_OR_RETURN(const int& value3, ReturnStatusOrValue(3));
    EXPECT_EQ(3, value3);
    ASSIGN_OR_RETURN(int value4 [[maybe_unused]],
                     ReturnStatusOrError("EXPECTED"));
    return ReturnError("ERROR");
  };

  EXPECT_THAT(func().message(), Eq("EXPECTED"));
}

TEST(AssignOrReturn, WorksWithReferences) {
  int value = 17;
  auto func = [&]() -> absl::Status {
    ASSIGN_OR_RETURN(int& value1, ReturnStatusOrRef(value));
    EXPECT_EQ(&value1, &value);

    ASSIGN_OR_RETURN(const int& value2, ReturnStatusOrRef(value));
    EXPECT_EQ(&value2, &value);

    ASSIGN_OR_RETURN(int value3, ReturnStatusOrRef(value));
    EXPECT_EQ(value3, value);

    value = 11;
    EXPECT_NE(value3, value);

    ASSIGN_OR_RETURN(int value4 [[maybe_unused]],
                     ReturnStatusOrError("EXPECTED"));
    return ReturnError("ERROR");
  };

  EXPECT_THAT(func().message(), Eq("EXPECTED"));
}

TEST(AssignOrReturn, WorksWithCommasInType) {
  auto func = []() -> absl::Status {
    ASSIGN_OR_RETURN((std::tuple<int, int> t1), ReturnStatusOrTupleValue(1, 1));
    EXPECT_EQ((std::tuple{1, 1}), t1);
    ASSIGN_OR_RETURN((const std::tuple<int, std::tuple<int, int>, int> t2),
                     ReturnStatusOrTupleValue(1, std::tuple{1, 1}, 1));
    EXPECT_EQ((std::tuple{1, std::tuple{1, 1}, 1}), t2);
    ASSIGN_OR_RETURN(
        (std::tuple<int, std::tuple<int, int>, int> t3),
        (ReturnStatusOrTupleError<int, std::tuple<int, int>, int>("EXPECTED")));
    t3 = {};  // fix unused error
    return ReturnError("ERROR");
  };

  EXPECT_THAT(func().message(), Eq("EXPECTED"));
}

TEST(AssignOrReturn, WorksWithStructureBindings) {
  auto func = []() -> absl::Status {
    ASSIGN_OR_RETURN((const auto& [t1, t2, t3, t4, t5]),
                     ReturnStatusOrTupleValue(std::tuple{1, 1}, 1, 2, 3, 4));
    EXPECT_EQ((std::tuple{1, 1}), t1);
    EXPECT_EQ(1, t2);
    EXPECT_EQ(2, t3);
    EXPECT_EQ(3, t4);
    EXPECT_EQ(4, t5);
    ASSIGN_OR_RETURN(int t6 [[maybe_unused]], ReturnStatusOrError("EXPECTED"));
    return ReturnError("ERROR");
  };

  EXPECT_THAT(func().message(), Eq("EXPECTED"));
}

TEST(AssignOrReturn, WorksWithParenthesesAndDereference) {
  auto func = []() -> absl::Status {
    int integer;
    int* pointer_to_integer = &integer;
    ASSIGN_OR_RETURN((*pointer_to_integer), ReturnStatusOrValue(1));
    EXPECT_EQ(1, integer);
    ASSIGN_OR_RETURN(*pointer_to_integer, ReturnStatusOrValue(2));
    EXPECT_EQ(2, integer);
    // Make the test where the order of dereference matters and treat the
    // parentheses.
    pointer_to_integer--;
    int** pointer_to_pointer_to_integer = &pointer_to_integer;
    ASSIGN_OR_RETURN((*pointer_to_pointer_to_integer)[1],
                     ReturnStatusOrValue(3));
    EXPECT_EQ(3, integer);
    ASSIGN_OR_RETURN(int t1 [[maybe_unused]], ReturnStatusOrError("EXPECTED"));
    return ReturnError("ERROR");
  };

  EXPECT_THAT(func().message(), Eq("EXPECTED"));
}

TEST(AssignOrReturn, WorksWithAppend) {
  auto fail_test_if_called = []() -> std::string {
    ADD_FAILURE();
    return "FAILURE";
  };
  auto func = [&]() -> absl::Status {
    int value [[maybe_unused]];
    ASSIGN_OR_RETURN(value, ReturnStatusOrValue(1), _ << fail_test_if_called());
    ASSIGN_OR_RETURN(value, ReturnStatusOrError("EXPECTED A"),
                     _ << "EXPECTED B");
    return ReturnOk();
  };

  EXPECT_THAT(func().message(),
              AllOf(HasSubstr("EXPECTED A"), HasSubstr("EXPECTED B")));
}

TEST(AssignOrReturn, WorksWithAdaptorFunc) {
  auto fail_test_if_called = [](util::StatusBuilder builder) {
    ADD_FAILURE();
    return builder;
  };
  auto adaptor = [](util::StatusBuilder builder) {
    return builder << "EXPECTED B";
  };
  auto func = [&]() -> absl::Status {
    int value [[maybe_unused]];
    ASSIGN_OR_RETURN(value, ReturnStatusOrValue(1), fail_test_if_called(_));
    ASSIGN_OR_RETURN(value, ReturnStatusOrError("EXPECTED A"), adaptor(_));
    return ReturnOk();
  };

  EXPECT_THAT(func().message(),
              AllOf(HasSubstr("EXPECTED A"), HasSubstr("EXPECTED B")));
}

TEST(AssignOrReturn, WorksWithThirdArgumentAndCommas) {
  auto fail_test_if_called = [](util::StatusBuilder builder) {
    ADD_FAILURE();
    return builder;
  };
  auto adaptor = [](util::StatusBuilder builder) {
    return builder << "EXPECTED B";
  };
  auto func = [&]() -> absl::Status {
    ASSIGN_OR_RETURN((const auto& [t1, t2, t3]),
                     ReturnStatusOrTupleValue(1, 2, 3), fail_test_if_called(_));
    EXPECT_EQ(t1, 1);
    EXPECT_EQ(t2, 2);
    EXPECT_EQ(t3, 3);
    ASSIGN_OR_RETURN((const auto& [t4, t5, t6]),
                     (ReturnStatusOrTupleError<int, int, int>("EXPECTED A")),
                     adaptor(_));
    // Silence errors about the unused values.
    static_cast<void>(t4);
    static_cast<void>(t5);
    static_cast<void>(t6);
    return ReturnOk();
  };

  EXPECT_THAT(func().message(),
              AllOf(HasSubstr("EXPECTED A"), HasSubstr("EXPECTED B")));
}

TEST(AssignOrReturn, WorksWithAppendIncludingLocals) {
  auto func = [&](absl::string_view str) -> absl::Status {
    int value [[maybe_unused]];
    ASSIGN_OR_RETURN(value, ReturnStatusOrError("EXPECTED A"), _ << str);
    return ReturnOk();
  };

  EXPECT_THAT(func("EXPECTED B").message(),
              AllOf(HasSubstr("EXPECTED A"), HasSubstr("EXPECTED B")));
}

TEST(AssignOrReturn, WorksForExistingVariable) {
  auto func = []() -> absl::Status {
    int value = 1;
    ASSIGN_OR_RETURN(value, ReturnStatusOrValue(2));
    EXPECT_EQ(2, value);
    ASSIGN_OR_RETURN(value, ReturnStatusOrValue(3));
    EXPECT_EQ(3, value);
    ASSIGN_OR_RETURN(value, ReturnStatusOrError("EXPECTED"));
    return ReturnError("ERROR");
  };

  EXPECT_THAT(func().message(), Eq("EXPECTED"));
}

TEST(AssignOrReturn, UniquePtrWorks) {
  auto func = []() -> absl::Status {
    ASSIGN_OR_RETURN(std::unique_ptr<int> ptr, ReturnStatusOrPtrValue(1));
    EXPECT_EQ(*ptr, 1);
    return ReturnError("EXPECTED");
  };

  EXPECT_THAT(func().message(), Eq("EXPECTED"));
}

TEST(AssignOrReturn, UniquePtrWorksForExistingVariable) {
  auto func = []() -> absl::Status {
    std::unique_ptr<int> ptr;
    ASSIGN_OR_RETURN(ptr, ReturnStatusOrPtrValue(1));
    EXPECT_EQ(*ptr, 1);

    ASSIGN_OR_RETURN(ptr, ReturnStatusOrPtrValue(2));
    EXPECT_EQ(*ptr, 2);
    return ReturnError("EXPECTED");
  };

  EXPECT_THAT(func().message(), Eq("EXPECTED"));
}

TEST(AssignOrReturn, PreservesExplicitlySetCanonicalCode) {
  // Set up a status with an explicitly set canonical code (i.e. one that is not
  // generated by the error space).
  absl::Status error = util::PosixErrorToStatus(ENOSYS, "enosys");
  util::SetCanonicalCode(absl::StatusCode::kPermissionDenied, &error);

  // Obtain the error via ASSIGN_OR_RETURN, exercising the various code paths
  // that add context.
  const absl::Status transformed = [error]() -> absl::Status {
    ASSIGN_OR_RETURN(int dummy, absl::StatusOr<int>(error),
                     ((_ << "foo").SetAppend() << "bar").SetPrepend() << "baz");

    // Silence errors about an unused value.
    static_cast<void>(dummy);
    return absl::OkStatus();
  }();

  // The canonical code should have been preserved.
  EXPECT_EQ(absl::StatusCode::kPermissionDenied, transformed.code());
}

TEST(AssignOrReturn, ChainSourceLocation) {
  auto func1 = []() -> absl::StatusOr<std::unique_ptr<int>> {
    std::unique_ptr<int> ptr;
    ASSIGN_OR_RETURN(ptr, ReturnStatusOrPtrValue(1));
    return ptr;
  };
  auto func2 = []() -> absl::StatusOr<std::unique_ptr<int>> {
    return absl::Status(absl::StatusCode::kInternal, "msg");
  };
  int func2_line = __builtin_LINE() - 2;

  auto func3 = [=]() -> absl::StatusOr<std::unique_ptr<int>> {
    std::unique_ptr<int> ptr;
    ASSIGN_OR_RETURN(ptr, func1());
    ASSIGN_OR_RETURN(ptr, func2());
    return ptr;
  };
  int func3_line = __builtin_LINE() - 3;

  auto func4 = [=]() -> absl::StatusOr<std::unique_ptr<int>> {
    std::unique_ptr<int> ptr;
    ASSIGN_OR_RETURN(ptr, func3());
    ASSIGN_OR_RETURN(ptr, func2());
    return ptr;
  };
  int func4_line = __builtin_LINE() - 4;

  absl::StatusOr<std::unique_ptr<int>> result = func4();
  EXPECT_EQ(absl::StatusCode::kInternal, result.status().code());
  CheckSourceLocation(result.status(), {func2_line, func3_line, func4_line});
}

TEST(AssignOrReturn, NotChainSourceLocationWithEmptyMsg) {
  auto func1 = []() -> absl::StatusOr<std::unique_ptr<int>> {
    std::unique_ptr<int> ptr;
    ASSIGN_OR_RETURN(ptr, ReturnStatusOrPtrValue(1));
    return ptr;
  };
  auto func2 = []() -> absl::StatusOr<std::unique_ptr<int>> {
    return absl::Status(absl::StatusCode::kInternal, "");
  };

  auto func3 = [=]() -> absl::StatusOr<std::unique_ptr<int>> {
    std::unique_ptr<int> ptr;
    ASSIGN_OR_RETURN(ptr, func1());
    ASSIGN_OR_RETURN(ptr, func2());
    return ptr;
  };

  auto func4 = [=]() -> absl::StatusOr<std::unique_ptr<int>> {
    std::unique_ptr<int> ptr;
    ASSIGN_OR_RETURN(ptr, func3());
    ASSIGN_OR_RETURN(ptr, func2());
    return ptr;
  };

  absl::StatusOr<std::unique_ptr<int>> result = func4();
  EXPECT_EQ(absl::StatusCode::kInternal, result.status().code());
  CheckSourceLocation(result.status());
}

TEST(AssignOrReturn, ChainSourceLocationWith3ArgStatusMacro) {
  auto func1 = []() -> absl::StatusOr<std::unique_ptr<int>> {
    std::unique_ptr<int> ptr;
    ASSIGN_OR_RETURN(ptr, ReturnStatusOrPtrValue(1));
    return ptr;
  };
  auto func2 = []() -> absl::StatusOr<std::unique_ptr<int>> {
    return absl::Status(absl::StatusCode::kInternal, "");
  };

  auto func3 = [=]() -> absl::StatusOr<std::unique_ptr<int>> {
    std::unique_ptr<int> ptr;
    ASSIGN_OR_RETURN(ptr, func1());
    ASSIGN_OR_RETURN(ptr, func2(), _ << "hmm");
    return ptr;
  };
  int func3_line = __builtin_LINE() - 3;

  auto func4 = [=]() -> absl::StatusOr<std::unique_ptr<int>> {
    std::unique_ptr<int> ptr;
    ASSIGN_OR_RETURN(ptr, func3());
    ASSIGN_OR_RETURN(ptr, func2());
    return ptr;
  };
  int func4_line = __builtin_LINE() - 4;

  absl::StatusOr<std::unique_ptr<int>> result = func4();
  EXPECT_EQ(absl::StatusCode::kInternal, result.status().code());
  CheckSourceLocation(result.status(), {func3_line, func4_line});
}

TEST(ReturnIfError, Works) {
  auto func = []() -> absl::Status {
    RETURN_IF_ERROR(ReturnOk());
    RETURN_IF_ERROR(ReturnOk());
    RETURN_IF_ERROR(ReturnError("EXPECTED"));
    return ReturnError("ERROR");
  };

  EXPECT_THAT(func().message(), Eq("EXPECTED"));
}

TEST(ReturnIfError, WorksWithSourceLocation) {
  {
    auto func = []() -> absl::Status {
      RETURN_IF_ERROR(ReturnOk());
      RETURN_IF_ERROR(ReturnOk());
      RETURN_IF_ERROR(absl::Status(absl::StatusCode::kUnknown, "EXPECTED"));
      return ReturnError("ERROR");
    };
    int error_line = __builtin_LINE() - 3;
    int func_line = error_line;

    EXPECT_THAT(func().message(), Eq("EXPECTED"));
    CheckSourceLocation(func(), {error_line, func_line});
  }
  {
    auto func1 = []() -> absl::Status {
      RETURN_IF_ERROR(ReturnOk());
      RETURN_IF_ERROR(ReturnOk());
      RETURN_IF_ERROR(absl::Status(absl::StatusCode::kUnknown, "EXPECTED"));
      return ReturnError("ERROR");
    };
    int error_line = __builtin_LINE() - 3;
    int func_line = error_line;

    auto func3 = [=]() -> absl::Status {
      RETURN_IF_ERROR(func1());
      return ReturnError("ERROR_Func3");
    };
    int func3_line = __builtin_LINE() - 3;

    auto func4 = [=]() -> absl::Status {
      RETURN_IF_ERROR(func3());
      return ReturnError("ERROR_Func4");
    };
    int func4_line = __builtin_LINE() - 3;
    CheckSourceLocation(func4(),
                        {error_line, func_line, func3_line, func4_line});
  }
  {
    auto func1 = []() -> absl::Status {
      RETURN_IF_ERROR(ReturnOk());
      RETURN_IF_ERROR(ReturnOk());
      RETURN_IF_ERROR(absl::Status(absl::StatusCode::kUnknown, ""));
      return ReturnError("ERROR");
    };

    auto func3 = [=]() -> absl::Status {
      RETURN_IF_ERROR(func1());
      return ReturnError("ERROR_Func3");
    };

    auto func4 = [=]() -> absl::Status {
      RETURN_IF_ERROR(func3());
      return ReturnError("ERROR_Func4");
    };
    CheckSourceLocation(func4());
  }
}

TEST(ReturnIfError, WorksWithSourceLocationOn2Arg) {
  {
    auto func = []() -> absl::Status {
      RETURN_IF_ERROR(ReturnOk());
      RETURN_IF_ERROR(ReturnOk());
      RETURN_IF_ERROR(absl::Status(absl::StatusCode::kUnknown, "EXPECTED"))
          << "foo";
      return ReturnError("ERROR");
    };
    int error_line = __builtin_LINE() - 4;
    int func_line = error_line;

    EXPECT_THAT(func().message(), Eq("EXPECTED; foo"));
    CheckSourceLocation(func(), {error_line, func_line});
  }
  {
    auto func1 = []() -> absl::Status {
      RETURN_IF_ERROR(ReturnOk());
      RETURN_IF_ERROR(ReturnOk());
      RETURN_IF_ERROR(absl::Status(absl::StatusCode::kUnknown, "EXPECTED"))
          << "foo";
      return ReturnError("ERROR");
    };
    int error_line = __builtin_LINE() - 4;
    int func_line = error_line;

    auto func3 = [=]() -> absl::Status {
      RETURN_IF_ERROR(func1());
      return ReturnError("ERROR_Func3");
    };
    int func3_line = __builtin_LINE() - 3;

    auto func4 = [=]() -> absl::Status {
      RETURN_IF_ERROR(func3());
      return ReturnError("ERROR_Func4");
    };
    int func4_line = __builtin_LINE() - 3;
    CheckSourceLocation(func4(),
                        {error_line, func_line, func3_line, func4_line});
  }
  {
    auto func1 = []() -> absl::Status {
      RETURN_IF_ERROR(ReturnOk());
      RETURN_IF_ERROR(ReturnOk());
      RETURN_IF_ERROR(absl::Status(absl::StatusCode::kUnknown, ""));
      return ReturnError("ERROR");
    };

    auto func3 = [=]() -> absl::Status {
      RETURN_IF_ERROR(func1()) << "foo";
      return ReturnError("ERROR_Func3");
    };
    int func3_line = __builtin_LINE() - 3;

    auto func4 = [=]() -> absl::Status {
      RETURN_IF_ERROR(func3());
      return ReturnError("ERROR_Func4");
    };
    int func4_line = __builtin_LINE() - 3;
    EXPECT_THAT(func4().message(), Eq("foo"));
    CheckSourceLocation(func4(), {func3_line, func4_line});
  }
  {
    auto func1 = []() -> absl::Status {
      RETURN_IF_ERROR(ReturnOk());
      RETURN_IF_ERROR(ReturnOk());
      RETURN_IF_ERROR(absl::Status(absl::StatusCode::kUnknown, "")) << "";
      return ReturnError("ERROR");
    };

    auto func3 = [=]() -> absl::Status {
      RETURN_IF_ERROR(func1());
      return ReturnError("ERROR_Func3");
    };

    auto func4 = [=]() -> absl::Status {
      RETURN_IF_ERROR(func3());
      return ReturnError("ERROR_Func4");
    };
    CheckSourceLocation(func4());
  }
}

TEST(ReturnIfError, WorksWithBuilder) {
  auto func = []() -> absl::Status {
    RETURN_IF_ERROR(ReturnOkBuilder());
    RETURN_IF_ERROR(ReturnOkBuilder());
    RETURN_IF_ERROR(ReturnErrorBuilder("EXPECTED"));
    return ReturnErrorBuilder("ERROR");
  };

  EXPECT_THAT(func().message(), Eq("EXPECTED"));

  // Test that RETURN_IF_ERROR can also return the StatusBuilder directly.
  auto func2 = []() -> util::StatusBuilder {
    RETURN_IF_ERROR(ReturnOkBuilder());
    RETURN_IF_ERROR(ReturnOkBuilder());
    RETURN_IF_ERROR(ReturnErrorBuilder("EXPECTED"));
    return ReturnErrorBuilder("ERROR");
  };
  EXPECT_THAT(static_cast<absl::Status>(func2()).message(), Eq("EXPECTED"));
}

TEST(ReturnIfError, IfInputIsBuilderDoesNotEagerlyConvertToStatus) {
  auto func = []() -> absl::Status {
    auto builder = ReturnErrorBuilder("FIRST");
    builder.SetPrepend();
    // If this call decays the builder to Status first and then makes another
    // builder it will forget about the SetPrepend and the streaming will happen
    // in the wrong order.
    RETURN_IF_ERROR(builder) << "SECOND ";
    return absl::OkStatus();
  };
  EXPECT_THAT(func().message(), Eq("SECOND FIRST"));
}

TEST(ReturnIfError, WorksWithLambda) {
  auto func = []() -> absl::Status {
    RETURN_IF_ERROR([] { return ReturnOk(); }());
    RETURN_IF_ERROR([] { return ReturnError("EXPECTED"); }());
    return ReturnError("ERROR");
  };

  EXPECT_THAT(func().message(), Eq("EXPECTED"));
}

TEST(ReturnIfError, WorksWithAppend) {
  auto fail_test_if_called = []() -> std::string {
    ADD_FAILURE();
    return "FAILURE";
  };
  auto func = [&]() -> absl::Status {
    RETURN_IF_ERROR(ReturnOk()) << fail_test_if_called();
    RETURN_IF_ERROR(ReturnError("EXPECTED A")) << "EXPECTED B";
    return absl::OkStatus();
  };

  EXPECT_THAT(func().message(),
              AllOf(HasSubstr("EXPECTED A"), HasSubstr("EXPECTED B")));
}

TEST(ReturnIfError, WorksWithVoidReturnAdaptor) {
  int code = 0;
  int phase = 0;
  auto adaptor = [&](absl::Status status) -> void { code = phase; };
  auto func = [&]() -> void {
    phase = 1;
    RETURN_IF_ERROR(ReturnOk()).With(adaptor);
    phase = 2;
    RETURN_IF_ERROR(ReturnError("EXPECTED A")).With(adaptor);
    phase = 3;
  };

  func();
  EXPECT_EQ(phase, 2);
  EXPECT_EQ(code, 2);
}

TEST(ReturnIfError, PreservesExplicitlySetCanonicalCode) {
  // Set up a status with an explicitly set canonical code (i.e. one that is not
  // generated by the error space).
  absl::Status error = util::PosixErrorToStatus(ENOSYS, "enosys");
  util::SetCanonicalCode(absl::StatusCode::kPermissionDenied, &error);

  // Obtain the error via RETURN_IF_ERROR, exercising the various code paths
  // that add context.
  const absl::Status transformed = [error]() -> absl::Status {
    struct Policy {
      absl::Status operator()(util::StatusBuilder builder) {
        return ((builder << "foo").SetAppend() << "bar").SetPrepend() << "baz";
      }
    };

    RETURN_IF_ERROR(error).With(Policy());
    return absl::OkStatus();
  }();

  // The canonical code should have been preserved.
  EXPECT_EQ(absl::StatusCode::kPermissionDenied, transformed.code());
}

// Basis for RETURN_IF_ERROR and ASSIGN_OR_RETURN benchmarks.  Derived
// classes override LoopAgain() with the macro invocation(s).
template <class T>
class ReturnLoop {
 public:
  using ReturnType = T;

  explicit ReturnLoop(ReturnType return_value)
      : value_(std::move(return_value)) {}
  virtual ~ReturnLoop() = default;

  ReturnType Loop(size_t* ops) {
    if (*ops == 0) {
      return value_;
    }
    // LoopAgain is virtual, with the intent that this defeats tail
    // recursion optimization.
    return LoopAgain(ops);
  }

 private:
  virtual ReturnType LoopAgain(size_t* ops) = 0;

  const ReturnType value_;
};

class ReturnIfErrorLoop : public ReturnLoop<absl::Status> {
 public:
  explicit ReturnIfErrorLoop(absl::Status return_value)
      : ReturnLoop(std::move(return_value)) {}

 private:
  absl::Status LoopAgain(size_t* ops) override {
    --*ops;
    RETURN_IF_ERROR(Loop(ops));
    return absl::OkStatus();
  }
};

class ReturnIfErrorWithAnnotateLoop : public ReturnLoop<absl::Status> {
 public:
  explicit ReturnIfErrorWithAnnotateLoop(absl::Status return_value)
      : ReturnLoop(std::move(return_value)) {}

 private:
  absl::Status LoopAgain(size_t* ops) override {
    --*ops;
    RETURN_IF_ERROR(Loop(ops))
        << "The quick brown fox jumped over the lazy dog.";
    return absl::OkStatus();
  }
};

class AssignOrReturnLoop : public ReturnLoop<absl::StatusOr<int>> {
 public:
  explicit AssignOrReturnLoop(ReturnType return_value)
      : ReturnLoop(std::move(return_value)) {}

 private:
  ReturnType LoopAgain(size_t* ops) override {
    --*ops;
    ASSIGN_OR_RETURN(int result, Loop(ops));
    return result;
  }

  ReturnType result_;
};

class AssignOrReturnAnnotateLoop : public ReturnLoop<absl::StatusOr<int>> {
 public:
  explicit AssignOrReturnAnnotateLoop(ReturnType return_value)
      : ReturnLoop(std::move(return_value)) {}

 private:
  ReturnType LoopAgain(size_t* ops) override {
    --*ops;
    ASSIGN_OR_RETURN(int result, Loop(ops),
                     _ << "The quick brown fox jumped over the lazy dog.");
    return result;
  }

  ReturnType result_;
};

absl::Status BenchmarkErrorStatus() {
  // This error message is intended to be long enough to guarantee external
  // memory allocation in std::string.
  return absl::Status(absl::StatusCode::kUnknown,
                      "The quick brown fox jumped over the lazy dog.");
}

// Drive a benchmark loop.  T is intended to be a ReturnLoop (above).
template <class T>
void BenchmarkLoop(T* driver, benchmark::State* state) {
  // Paranoia: induce the compiler to give up and assume driver is an
  // arbitrary type.
  benchmark::DoNotOptimize(driver);

  // We benchmark 8 macro invocations (stack depth) per loop.  This
  // amortizes one time costs (e.g. building the initial error value)
  // across what we actually care about.
  const int max_ops = 8;
  while (state->KeepRunningBatch(max_ops)) {
    size_t ops = max_ops;
    benchmark::DoNotOptimize(driver->Loop(&ops));
  }
}

void BM_ReturnIfError_Ok(benchmark::State& state) {
  ReturnIfErrorLoop loop(absl::OkStatus());
  BenchmarkLoop(&loop, &state);
}
BENCHMARK(BM_ReturnIfError_Ok);

void BM_ReturnIfError_Error(benchmark::State& state) {
  ReturnIfErrorLoop loop(BenchmarkErrorStatus());
  BenchmarkLoop(&loop, &state);
}
BENCHMARK(BM_ReturnIfError_Error);

void BM_ReturnIfError_Annotate_Ok(benchmark::State& state) {
  ReturnIfErrorWithAnnotateLoop loop(absl::OkStatus());
  BenchmarkLoop(&loop, &state);
}
BENCHMARK(BM_ReturnIfError_Annotate_Ok);

void BM_ReturnIfError_Annotate_Error(benchmark::State& state) {
  ReturnIfErrorWithAnnotateLoop loop(BenchmarkErrorStatus());
  BenchmarkLoop(&loop, &state);
}
BENCHMARK(BM_ReturnIfError_Annotate_Error);

void BM_AssignOrReturn_Ok(benchmark::State& state) {
  const int ok = 5;  // arbitrary value
  AssignOrReturnLoop loop(ok);
  BenchmarkLoop(&loop, &state);
}
BENCHMARK(BM_AssignOrReturn_Ok);

void BM_AssignOrReturn_Error(benchmark::State& state) {
  AssignOrReturnLoop loop(BenchmarkErrorStatus());
  BenchmarkLoop(&loop, &state);
}
BENCHMARK(BM_AssignOrReturn_Error);

void BM_AssignOrReturn_Annotate_Ok(benchmark::State& state) {
  const int ok = 5;  // arbitrary value
  AssignOrReturnAnnotateLoop loop(ok);
  BenchmarkLoop(&loop, &state);
}
BENCHMARK(BM_AssignOrReturn_Annotate_Ok);

void BM_AssignOrReturn_Annotate_Error(benchmark::State& state) {
  AssignOrReturnAnnotateLoop loop(BenchmarkErrorStatus());
  BenchmarkLoop(&loop, &state);
}
BENCHMARK(BM_AssignOrReturn_Annotate_Error);

ABSL_ATTRIBUTE_NOINLINE absl::StatusOr<int> DummyMakeStatusOr() { return 0; }
ABSL_ATTRIBUTE_NOINLINE absl::Status DummyMakeStatus() {
  return absl::OkStatus();
}

// Dummy stubs that exercise the macros to have something to dump and inspect
// the assembly.
// Can be dumped like:
//  $ lldb ${BINARY} --one-line "disassemble --name ${FUNC}" --batch
absl::Status Codegen_Control_Status_Status(int& out) {
  if (auto status = DummyMakeStatus(); ABSL_PREDICT_FALSE(!status.ok())) {
    status.AddSourceLocation(absl::SourceLocation::current());
    return status;
  }
  out = 17;  // just some work to do conditionally.
  return absl::OkStatus();
}

absl::Status Codegen_RETURN_IF_ERROR_Status(int& out) {
  RETURN_IF_ERROR(DummyMakeStatus());
  out = 17;  // just some work to do conditionally.
  return absl::OkStatus();
}

absl::Status Codegen_RETURN_IF_ERROR_Status_Stream(int& out) {
  // We perform many operations in the builder to make sure they don't cause
  // spills or escape the object.
  RETURN_IF_ERROR(DummyMakeStatus()).LogError().SetAppend().SetPrepend()
      << "Some string";
  out = 17;  // just some work to do conditionally.
  return absl::OkStatus();
}

absl::StatusOr<int> Codegen_Control_Status_StatusOr(int x) {
  if (auto status = DummyMakeStatus(); ABSL_PREDICT_FALSE(!status.ok())) {
    status.AddSourceLocation(absl::SourceLocation::current());
    return status;
  }
  return ~x;
}

absl::StatusOr<int> Codegen_RETURN_IF_ERROR_StatusOr(int x) {
  RETURN_IF_ERROR(DummyMakeStatus());
  return ~x;
}

absl::Status Codegen_Control_StatusOr_Status(int& out) {
  if (auto res = DummyMakeStatusOr(); ABSL_PREDICT_FALSE(!res.ok())) {
    return absl::Status(std::move(res).status(),
                        absl::SourceLocation::current());
  } else {
    out = *res;
  }
  return absl::OkStatus();
}

absl::Status Codegen_ASSIGN_OR_RETURN_Status(int& out) {
  ASSIGN_OR_RETURN(out, DummyMakeStatusOr());
  return absl::OkStatus();
}

absl::StatusOr<int> Codegen_Control_StatusOr_StatusOr() {
  if (auto res = DummyMakeStatusOr(); ABSL_PREDICT_FALSE(!res.ok())) {
    return absl::Status(std::move(res).status(),
                        absl::SourceLocation::current());
  } else {
    return ~*res;
  }
}

absl::Status Codegen_Control_BoundsCheck(int* ptr, int a, int b) {
  if (a < b) {
    return absl::InternalError("");
  }
  *ptr = a - b;
  return absl::OkStatus();
}

absl::Status CodegenBoundCheck(int a, int b) {
  if (a < b) return absl::InternalError("");
  return absl::OkStatus();
}

absl::Status Codegen_RETURN_IF_ERROR_BoundsCheck(int* ptr, int a, int b) {
  RETURN_IF_ERROR(CodegenBoundCheck(a, b));
  *ptr = a - b;
  return absl::OkStatus();
}

absl::StatusOr<int> Codegen_ASSIGN_OR_RETURN_StatusOr() {
  ASSIGN_OR_RETURN(int x, DummyMakeStatusOr());
  return ~x;
}

absl::StatusOr<int> Codegen_RETURN_IF_ERROR_LValue() {
  auto result = DummyMakeStatusOr();
  RETURN_IF_ERROR(result.status());
  return ~*result;
}

absl::Status Codegen_StatusFactories_Status(bool b) {
  if (b) {
    return absl::InternalError("Foo");
  }
  return absl::OkStatus();
}

absl::StatusOr<int> Codegen_StatusFactories_StatusOr(bool b) {
  if (b) {
    return absl::InternalError("Foo");
  }
  return 17;
}

// DoNotOptimize to make sure the functions are generated in the library.
int codegen_dummy =
    (benchmark::DoNotOptimize(std::make_tuple(
         Codegen_Control_Status_Status, Codegen_RETURN_IF_ERROR_Status,
         Codegen_RETURN_IF_ERROR_Status_Stream, Codegen_Control_Status_StatusOr,
         Codegen_RETURN_IF_ERROR_LValue, Codegen_RETURN_IF_ERROR_StatusOr,
         Codegen_Control_StatusOr_Status, Codegen_ASSIGN_OR_RETURN_Status,
         Codegen_Control_StatusOr_StatusOr, Codegen_ASSIGN_OR_RETURN_StatusOr,
         Codegen_StatusFactories_Status, Codegen_StatusFactories_StatusOr,
         Codegen_Control_BoundsCheck, Codegen_RETURN_IF_ERROR_BoundsCheck)),
     1);

}  // namespace
