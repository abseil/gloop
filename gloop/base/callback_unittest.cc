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

#include "gloop/base/callback.h"

#include <string>

#include "absl/functional/bind_front.h"
#include "absl/log/check.h"
#include "absl/log/log.h"
#include "gloop/util/functional/from_callback.h"
#include "gloop/util/functional/to_callback.h"
#include "gtest/gtest.h"

namespace {
struct Response {
  int key_;
  std::string value_;
};

class Server {
 public:
  void StartLookup(::util::functional::CallbackFunctor<Response> cb, int key) {
    Response r = {key, std::string()};  // Simply send back the key.
    if (cb) (cb)(r);                    // let the client run.
  }
};

class Client {
 public:
  explicit Client(Server* s) : key_(0), server_(s) {}

  void Lookup() {
    server_->StartLookup(util::functional::WithCurrentContext(
                             absl::bind_front(&Client::LookupDone, this)),
                         key_);
  }

  void NullLookup() { server_->StartLookup(nullptr, key_); }

  // callback function by the server when lookup is done.
  void LookupDone(Response resp) {
    CHECK_EQ(key_, resp.key_);
    LOG(INFO) << "Lookup done with " << key_;
    key_++;
    if (key_ > 3) return;
    Lookup();
  }

 private:
  int key_;
  Server* server_;
};

TEST(Callback, Callback) {
  Server s;
  Client c(&s);
  c.Lookup();

  LOG(INFO) << "Doing NULL callback test ";
  c.NullLookup();  // make sure NULL callback won't crash the system
  LOG(INFO) << "Done callback test ";
}

int GetStringLength(const char* s) { return 0; }

TEST(Callback, AllowsTypeConversionForPreboundArgsInFreeFunction) {
  char* s = nullptr;
  ::util::functional::ResultCallbackFunctor<int> cb =
      util::functional::ToPermanentCallback<
          ::util::functional::ResultCallbackFunctor<int>>(
          absl::bind_front(GetStringLength, s));
  (cb)();
}

}  // namespace
