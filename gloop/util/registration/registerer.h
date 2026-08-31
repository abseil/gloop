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

// -----------------------------------------------------------------------------
//                   __                                __           __
//              ____/ /__  ____  ________  _________ _/ /____  ____/ /
//    ______   / __  / _ \/ __ \/ ___/ _ \/ ___/ __ `/ __/ _ \/ __  /  ______
//   /_____/  / /_/ /  __/ /_/ / /  /  __/ /__/ /_/ / /_/  __/ /_/ /  /_____/
//            \__,_/\___/ .___/_/   \___/\___/\__,_/\__/\___/\__,_/
//                     /_/
//
// Don't use this registration system in new code! Macro-based registration
// generally requires defining new macros in header files, which is disallowed
// by <link>. Use FunctionRegistry
// (//gloop/util/registration/function_registry.h) instead.
//
// -----------------------------------------------------------------------------
//
// Preprocessor and template gymnastics for registering classes by name, and for
// registering aliases for names. We support registration of classes requiring
// parameters in the constructor. This class is thread-safe.
//
// Example 1:
//
// class MyBaseClass {
//  public:
//   MyBaseClass();
//   ...
// };
//
// DEFINE_REGISTERER(MyBaseClass);
// #define REGISTER_MYBASECLASS(name) REGISTER_ENTITY(name, MyBaseClass);
//
// class MyDerivedClass : public MyBaseClass {
//  public:
//   MyDerivedClass();
//   ...
// };
//
// REGISTER_MYBASECLASS(MyDerivedClass);
//
//   ...
//   CHECK(MyBaseClassRegisterer::IsValidName("MyDerivedClass"));
//   ...
//   // this creates an instance of MyDerivedClass
//   MyBaseClass *test =
//       MyBaseClassRegisterer::CreateByNameOrDie("MyDerivedClass");
//   ...
//   // get a factory for MyDerivedClass objects
//   MyBaseClassCreator* creator =
//     MyBaseClassRegisterer::GetByNameOrDie("MyDerivedClass");
//   // this creates another instance of MyDerivedClass
//   MyBaseClass *test2 = creator->Run();
//
//
// Example 2:
//
// class Request {
//  public:
//   Request(const Command &cmd, Result *res);
//   ...
// };
//
// // the 2 extra parameters are the argument types of the constructor
// DEFINE_REGISTERER(Request, const Command &, Result *);
//
// #define REGISTER_REQUEST(name) REGISTER_ENTITY(name, Request)
//
// class MyRequest : public Request {
//  public:
//   MyRequest(const Command &cmd, Result *res);
// };
//
// REGISTER_REQUEST(MyRequest);
//
//   ...
//   const Command &cmd = ...;
//   Result *res = new Result();
//   // this creates an instance of MyRequest
//   Request *request =
//     RequestRegisterer::CreateByNameOrDie("MyRequest", cmd, res);
//
// // Hint: If your class does not seem to be registered, try alwayslink=1 in
// // your build entry for the library that registers your class. With
// // registries, under certain cases, the compiler may (wrongly) deduce that
// // nothing in the library are used and may decide not to link it.
//
// =============================================================================
//
// Registered classes may have aliases (arbitrary strings by which the classes
// can be identified). The alias registration process is independent of the
// number of parameters required by the constructor, so there is only one
// REGISTER_ALIAS macro and it works with the REGISTER_ENTITY macro. Any
// number of aliases may be registered for each name, but multiple aliases for
// the same name must be registered on different lines (i.e. usage of a macro
// like REGISTER_WITH_TWO_ALIASES(name, alias1, alias2) is not supported and
// will result in an object-redefinition compiler error). Registration of the
// same alias multiple times (for the same name) will result in a single
// registration of the alias.
//
// Example:
//
// class Classifier {
//  public:
//   Classifier();
//   ...
// };
//
// DEFINE_REGISTERER(Classifier);
// DEFINE_ALIAS_REGISTERER(Classifier);
//
// // The next line needs to end with a backslash, but we can't explicitly
// // write \ at the end of a comment line because of C++ lexing rules.
// #define REGISTER_CLASSIFIER(name, alias) BACKSLASH
//   REGISTER_ENTITY(name, Classifier); BACKSLASH
//   REGISTER_ALIAS(name, alias, Classifier)
// #define REGISTER_ADDITIONAL_CLASSIFIER_ALIAS(name, alias) BACKSLASH
//   REGISTER_ALIAS(name, alias, Classifier)
//
// class LinearClassifier : public Classifier {
//  public:
//   LinearClassifier();
// };
//
// REGISTER_CLASSIFIER(LinearClassifier, "linear");
// REGISTER_ADDITIONAL_CLASSIFIER_ALIAS(LinearClassifier, "hyperplane");
//
//   ...
//   // this creates an instance of LinearClassifier
//   Classifier *classifier1 =
//     ClassifierRegisterer::CreateByNameOrDie("LinearClassifier");
//   // this creates another instance of LinearClassifier
//   string name = ClassifierAliasRegisterer::GetNameByAliasOrDie("linear");
//   Classifier *classifier2 = ClassifierRegisterer::CreateByNameOrDie(name);
//   // this creates yet another intance of LinearClassifier
//   name = ClassifierAliasRegisterer::GetNameByAliasOrDie("hyperplane");
//   Classifier *classifier3 = ClassifierRegisterer::CreateByNameOrDie(name);
//
//
// This is especially useful for replacing code like the following in a
// backwards-compatible way:
// if (classifier_type == string("linear"))
//   return new LinearClassifier();
// ...
//
// If e.g. LinearClassifier is registered by
// REGISTER_CLASSIFIER(LinearClassifier, "linear"), this would become:
// CHECK(ClassifierAliasRegisterer::IsValidAlias(classifier_type));
// const string& name =
//     ClassifierAliasRegisterer::GetNameByAliasOrDie(classifier_type);
// return ClassifierRegisterer::CreateByNameOrDie(name);
//
// Similarly, it is useful for replacing code like the following in a
// backwards-compatible way:
// switch(protobuf.classifier_type_enum()) {
//  case ClassifierProtobuf::LINEAR:
//   return new LinearClassifier();
//  ...
//
// If e.g. LinearClassifier is registered by
// REGISTER_CLASSIFIER(LinearClassifier, "LINEAR"), this would become:
// string classifier_type(
//   ClassifierProtobuf::ClassifierType_Name(protobuf.classifier_type_enum()));
// CHECK(ClassifierAliasRegisterer::IsValidAlias(classifier_type));
// const string& name =
//     ClassifierAliasRegisterer::GetNameByAliasOrDie(classifier_type);
// return ClassifierRegister::CreateByNameOrDie(name);
//
// =============================================================================
//
// The registration mechanism described above assumes that the
// registered subclasses have public constructors.
//
// This makes argument-checking cumbersome, as it requires you to
// either CHECK in the constructor (which may be undesirable if you
// want to handle the error gracefully) or to have a validation method
// on the already constructed object (which requires your object to
// support invalid state and check for that on all calls to its
// method).
//
// Instead, you can create a static factory method or function and use
// that instead of your constructor. It will still need to receive the
// same number of arguments as if you used a constructor but can
// choose to return nullptr or something else.
//
// Example:
//
// class Request {
//  public:
//   Request(const Command &cmd, Result *res);
//   ...
// };
//
// // We define the registerer as usual.
// DEFINE_REGISTERER(Request, const Command &, Result *);
//
// #define REGISTER_REQUEST(name, FactoryName) BACKSLASH
//   REGISTER_FACTORY_ENTITY(name, Request, FactoryName);
//
// class MyRequest : public Request {
//  public:
//    static Request *CreateOrNull(const Command &cmd, Result *res);
//
//  private:
//   MyRequest(const Command &cmd, Result *res);
// };
//
// REGISTER_REQUEST(MyRequest, MyRequest::CreateOrNull);
//
// We can now define MyRequest::CreateOrNull to validate cmd and res,
// then call the private constructor, and return the newly allocated
// Request object.
//
// Please note that the third argument to REGISTER_FACTORY_ENTITY
// must be the name of a static method or function,
// e.g. CreateMyRequest, or MyRequest::CreateOrNull.
//
// Also note that while the convention is that the first argument to
// REGISTER_FACTORY_ENTITY be the registered class name, the only strict
// requirement is that it will be unique amongst all files that may be linked
// to the same binary. It might be desirable to use a different name if the
// factory can return objects of more than one subclass of the base class.
//
// =============================================================================
//
// The mechanism described above for static factory methods/functions assumes
// that the factory returns a T*. However, it's often useful to return a
// different type S that depends on T, such as StatusOr<T*> (in order to provide
// detailed error messages). This can be achieved by creating a registry of
// S objects.
//
// Example:
//
// class Request {
//  public:
//   Request(const Command &cmd, Result *res);
//   ...
// };
//
// // Note: the same variadic macro is used for any number of arguments.
// DEFINE_FACTORY_REGISTERER(Request, absl::StatusOr<Request*>,
//                           const Command&, Result*);
//
// #define REGISTER_REQUEST(name, FactoryName) BACKSLASH
//   REGISTER_FACTORY_ENTITY(name, Request, FactoryName)
//
// class MyRequest : public Request {
//  public:
//    // The factory method returns the type specified in the second argument
//    // to DEFINE_FACTORY_REGISTERER.
//    static StatusOr<Request*> Create(const Command &cmd, Result *res);
//
//  private:
//   MyRequest(const Command &cmd, Result *res);
// };
//
// REGISTER_REQUEST(MyRequest, MyRequest::Create);
//
// We can now define MyRequest::Create to validate cmd and res, return a
// detailed Status on error, and call the private constructor on success.
//
// =============================================================================
//
// The registration mechanisms described above assume that the
// registered subclasses either have constructors or static factory
// methods/functions which take no arguments (e.g. MyDerivedClass), or
// that if there are arguments (e.g. MyResult), then the client
// instatiating the object (or calling the static factory
// method/function) must supply them all.
//
// This makes dependency injection difficult. For example, suppose
// MyDerivedClass internally depended on a FileSystem interface. It
// would be convenient to set it up with a MockFileSystem when
// supplying it to some client code for testing. If we passed the
// filesystem object in with the constructor, then it would require
// the client to know about this internal dependency.
//
// To solve this, besides the global static registration via the
// REGISTER_FOO macros, we also provide the NEW_ENTITY_REGISTRATION_CB
// macro, which supports registration via local instantiations of the
// Registerer classes which take a factory callback. For example:
//
// class MyBaseClass {
//  public:
//   MyBaseClass(FileSystem* fs);
//   ...
// };
//
// DEFINE_REGISTERER(MyBaseClass);
// #define NEW_MYBASECLASS_REGISTRATION(name, cb) BACKSLASH
//   NEW_ENTITY_REGISTRATION_CB(name, MyBaseClass, cb)
//
// class MyDerivedClass : public MyBaseClass {
//  public:
//   MyDerivedClass(FileSystem* fs) : MyBaseClass(fs) {}
//   ...
// }; // NOTE: We DO NOT need the REGISTER_FOO macros.
//
// We create a factory method that can be wrapped in a callback:
//
// static MyBaseClass* MyDerivedClassFactory(FileSystem* fs) {
//   return new MyDerivedClass(fs);
// }
//
// This method is then supplied to the instantiated registerer, with
// the appropriate filesystem dependency wrapped in the callback
// object.
//
//   ...
//   RealFileSystem fs;
//   scoped_ptr<MyBaseClassRegisterer> registerer(
//       NEW_MYBASECLASS_REGISTRATION("MyDerivedClass",
//           NewPermanentCallback(&MyDerivedClassFactory,
//                                implicit_cast<FileSystem*>(&fs))));
//
// The client code still knows nothing about this dependency and
// instantiates the class in the regular way with no arguments
// (the following code is identical to that in Example 1 above):
//
//   ...
//   CHECK(MyBaseClassRegisterer::IsValidName("MyDerivedClass"));
//   ...
//   // this creates an instance of MyDerivedClass
//   MyBaseClass *test =
//       MyBaseClassRegisterer::CreateByNameOrDie("MyDerivedClass");
//   ...
//   // get a factory for MyDerivedClass objects
//   MyBaseClassCreator* creator =
//     MyBaseClassRegisterer::GetByNameOrDie("MyDerivedClass");
//   // this creates another instance of MyDerivedClass
//   MyBaseClass *test2 = creator->Run();
//
// But if we had wanted to inject a mock filesystem instead, we would
// have created the registerer object as follows:
//
//   ...
//   MockFileSystem fs;
//   scoped_ptr<MyBaseClassRegisterer> registerer(
//       NEW_MYBASECLASS_REGISTRATION("MyDerivedClass",
//           NewPermanentCallback(&MyDerivedClassFactory,
//                                implicit_cast<FileSystem*>(&fs))));
//
// and the client code would be none the wiser.
//
// We can also rewrite Example 2 in this vein:
//
// class Request {
//  public:
//   Request(FileSystem* fs, const Command &cmd, Result *res);
//   ...
// };
//
// DEFINE_REGISTERER(Request, const Command &, Result *);
// #define NEW_REQUEST_REGISTRATION(name, cb) BACKSLASH
//   NEW_ENTITY_REGISTRATION_CB(name, Request, cb)
//
// class MyRequest : public Request {
//  public:
//   MyRequest(FileSystem *fs, const Command &cmd, Result *res);
// };
//
// static Request* MyRequestFactory(FileSystem *fs, const Command &cmd,
//                                  Result *res) {
//   return new MyRequest(fs, cmd, res);
// }
//   ...
//   MockFileSystem fs;
//   scoped_ptr<RequestRegisterer> registerer(
//       NEW_REQUEST_REGISTRATION("MyRequest",
//           NewPermanentCallback(&MyRequestFactory,
//                                implicit_cast<FileSystem*>(&fs))));
//
// Again, the client code instantiating the object is identical to the
// previous example and knows nothing of the injected dependency.
//   ...
//   const Command &cmd = ...;
//   Result *res = new Result();
//   // this creates an instance of MyRequest
//   Request *request =
//     RequestRegisterer::CreateByNameOrDie("MyRequest", cmd, res);
//
// See registerer_unittest.cc for the code in action.
//
// For mapping from types to type names/identifiers, see the related mechanisms
// implemented in //gloop/util/registration/typename.h and
// https://github.com/abseil/gloop/tree/main/gloop/util/gtl/typeid.h.

#ifndef THIRD_PARTY_GLOOP_UTIL_REGISTRATION_REGISTERER_H_
#define THIRD_PARTY_GLOOP_UTIL_REGISTRATION_REGISTERER_H_

#include <algorithm>
#include <atomic>
#include <functional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "absl/base/attributes.h"
#include "absl/base/thread_annotations.h"
#include "absl/strings/string_view.h"
#include "absl/synchronization/mutex.h"
#include "gloop/util/gtl/lockfree_hashmap.h"
#include "gloop/util/hash/transparent_hash.h"

namespace util_registration {

namespace internal {

class Registry {
 public:
  struct ObjectAndMetadata {
    ObjectAndMetadata(void* object, absl::string_view filename)
        : object(object), filename(filename), used(false) {}

    void* object;
    std::string filename;
    mutable std::atomic<bool> used;
  };

  ~Registry() = delete;

  Registry();

  // Returns false if the ownership of the object could not be assumed,
  // due to collisions.
  bool Insert(absl::string_view name, void* object, absl::string_view filename);

  // Returns ownership of the object back to the caller.
  void* Erase(absl::string_view name);

  const ObjectAndMetadata& Lookup(absl::string_view name) const;

  bool Contains(absl::string_view name) const;

  std::vector<std::string> GetNames() const;

  static std::vector<const Registry*> GetAllRegistries();

 private:
  gtl::LockFreeHashMap<std::string, ObjectAndMetadata> objects_;
  const Registry* registries_next_;
  static absl::Mutex registries_mutex_;
  static const Registry* registries_list_ ABSL_GUARDED_BY(registries_mutex_);
};

template <class T>
Registry& GetRegistry() {
  static const auto r = new internal::Registry();
  return *r;
}

class AliasRegistry {
 public:
  using NameFilePair = std::pair<std::string, std::string>;

  AliasRegistry();
  ~AliasRegistry();

  void Insert(absl::string_view alias, absl::string_view name,
              absl::string_view filename);

  void Erase(absl::string_view alias) { aliases_.erase(alias); }

  bool Contains(absl::string_view name) const;

  const std::string& Lookup(absl::string_view alias) const;

  void GetAliases(std::vector<std::string>* aliases) const;

 private:
  gtl::LockFreeHashMap<std::string, NameFilePair, util_hash::StringHash,
                       util_hash::StringEq>
      aliases_;
};

template <class T>
AliasRegistry& GetAliasRegistry() {
  static const auto r = new internal::AliasRegistry();
  return *r;
}

// An empty token signifying that a value was registered statically.
struct StaticRegistrationToken {};

// Takes ownership of "creator".
template <class R>
StaticRegistrationToken RegisterStatically(
    absl::string_view name, absl::string_view filename,
    typename R::CreatorFunction creator) {
  // Move creator to the heap, since Insert requires a pointer
  typename R::CreatorFunction* creator_ptr = new typename R::CreatorFunction;
  *creator_ptr = std::move(creator);
  if (!GetRegistry<typename R::CreatorFunction>().Insert(name, creator_ptr,
                                                         filename)) {
    delete creator_ptr;
  }
  return {};
}

template <class T>
StaticRegistrationToken RegisterAliasStatically(absl::string_view name,
                                                absl::string_view filename,
                                                absl::string_view alias) {
  if (!alias.empty()) {
    GetAliasRegistry<T>().Insert(alias, name, filename);
  }
  return {};
}

}  // namespace internal

template <class T>
class Registerer {
 public:
  // Register an object, with the provided key. The filename is stored
  // internally and helps when debugging conflicts. Takes ownership of
  // "object".
  Registerer(absl::string_view name, absl::string_view filename, T* object)
      : name_(name) {
    if (!internal::GetRegistry<T>().Insert(name_, object, filename)) {
      delete object;
    }
  }

  Registerer(absl::string_view name, absl::string_view filename, T object)
      : name_(name) {
    T* t_ptr = new T;
    *t_ptr = std::move(object);
    if (!internal::GetRegistry<T>().Insert(name_, t_ptr, filename)) {
      delete t_ptr;
    }
  }

  Registerer(const Registerer&) = delete;
  Registerer& operator=(const Registerer&) = delete;

  // Deleting a registerer will delete the mapping to the object it
  // registered.
  ~Registerer() {
    delete static_cast<T*>(internal::GetRegistry<T>().Erase(name_));
  }

  // Return the object registered with this name
  static T* GetByNameOrDie(absl::string_view name) {
    const auto& item = internal::GetRegistry<T>().Lookup(name);
    item.used.store(true, std::memory_order_relaxed);
    return static_cast<T*>(item.object);
  }

  // Return the filename of the object registered with this name,
  // which can, for example, be used to create links on statusz pages.
  static std::string GetFilenameByNameOrDie(absl::string_view name) {
    return internal::GetRegistry<T>().Lookup(name).filename;
  }

  // List all registered names into the names vector, in
  // lexicographically increasing order.  Any existing elements in the
  // vector are obliterated.
  static void GetNames(std::vector<std::string>* names) {
    *names = internal::GetRegistry<T>().GetNames();
  }

  // Returns a vector of all registered names sorted in lexicographically
  // increasing order.
  static std::vector<std::string> RegisteredNames() {
    return internal::GetRegistry<T>().GetNames();
  }

  // Returns true if an object with this name has been registered
  static bool IsValidName(absl::string_view name) {
    return internal::GetRegistry<T>().Contains(name);
  }

 private:
  const std::string name_;
};

template <class T>
class AliasRegisterer {
 public:
  using ObjectType = T;

  // Register an alias for the provided name. The filename is stored internally
  // and helps when debugging conflicts.
  AliasRegisterer(absl::string_view name, absl::string_view filename,
                  absl::string_view alias)
      : name_(name), alias_(alias) {
    if (!alias.empty()) {
      internal::GetAliasRegistry<T>().Insert(alias, name, filename);
    }
  }

  // Deleting an alias registerer will delete the mapping to the name it
  // aliased.
  ~AliasRegisterer() {
    if (!alias_.empty()) internal::GetAliasRegistry<T>().Erase(alias_);
  }

  AliasRegisterer(const AliasRegisterer&) = delete;
  AliasRegisterer& operator=(const AliasRegisterer&) = delete;

  // Returns the name registered with this alias.
  static const std::string& GetNameByAliasOrDie(absl::string_view alias) {
    return internal::GetAliasRegistry<T>().Lookup(alias);
  }

  // Lists all registered aliases into the aliases vector, in lexicographically
  // increasing order.  Any existing elements in the vector are obliterated.
  static void GetAliases(std::vector<std::string>* aliases) {
    internal::GetAliasRegistry<T>().GetAliases(aliases);
  }

  // Returns true if an object with this alias has been registered.
  static bool IsValidAlias(absl::string_view alias) {
    return internal::GetAliasRegistry<T>().Contains(alias);
  }

 private:
  const std::string name_;
  const std::string alias_;
};

template <class S, class... Args>
class FactoryRegisterer : public Registerer<std::function<S(Args...)>> {
 public:
  using CreatorFunction = std::function<S(Args...)>;

  FactoryRegisterer(absl::string_view name, absl::string_view file,
                    CreatorFunction creator)
      : Registerer<CreatorFunction>(name, file, creator) {}

  FactoryRegisterer(const FactoryRegisterer&) = delete;
  FactoryRegisterer& operator=(const FactoryRegisterer&) = delete;

  static S CreateByNameOrDie(absl::string_view name, Args... args) {
    return (*Registerer<CreatorFunction>::GetByNameOrDie(name))(
        std::forward<Args>(args)...);
  }
};

// A registerer for a specific base class.
template <class C, class... Args>
class ClassRegisterer : public FactoryRegisterer<C*, Args...> {
 public:
  using CreatedType = C;
  using FactoryRegisterer<C*, Args...>::FactoryRegisterer;

  // Returns a new object of the given type, which must be a subclass of C.
  template <class T>
  static C* Create(Args... args) {
    return new T(std::forward<Args>(args)...);
  }
};

// A wrapper around a ClassRegisterer subclass, providing a constructor
// where the first two arguments may be of any types convertible to string
// (including const char*). This helps remove temporaries at call sites when
// called with string literals as arguments, and avoid excessively large stack
// frames in static initialization. The third ctor argument is the creator
// function or callback.
template <class R>
class RegistererWrapper {
 public:
  template <class N, class F>
  RegistererWrapper(const N& name, const F& file,
                    typename R::CreatorFunction creator)
      : wrapped_(name, file, creator) {}

  RegistererWrapper(const RegistererWrapper&) = delete;
  RegistererWrapper& operator=(const RegistererWrapper&) = delete;

 private:
  R wrapped_;
};

// A wrapper around an AliasRegisterer subclass, similar to RegistererWrapper.
template <class R>
class AliasRegistererWrapper {
 public:
  template <class N, class F, class A>
  AliasRegistererWrapper(const N& name, const F& file, const A& alias)
      : wrapped_(name, file, alias) {}

  AliasRegistererWrapper(const AliasRegistererWrapper&) = delete;
  AliasRegistererWrapper& operator=(const AliasRegistererWrapper&) = delete;

 private:
  R wrapped_;
};

}  // namespace util_registration

// Two levels of macros are required to catenate tokens.
#define INTERNAL_REGISTER_CAT_INNER(x, y) x##y
#define INTERNAL_REGISTER_CAT(x, y) INTERNAL_REGISTER_CAT_INNER(x, y)

#define DEFINE_ALIAS_REGISTERER(ClassName)                       \
  class ClassName##AliasRegisterer                               \
      : public ::util_registration::AliasRegisterer<ClassName> { \
   public:                                                       \
    using AliasRegisterer::AliasRegisterer;                      \
  }

#define INTERNAL_REGISTERER_CLASS_SUFFIX(Suffix, ClassName, ...) \
  ClassName##Suffix

// The ellipsis below contains the class name, followed by the constructor
// argument types, if any.
#define DEFINE_REGISTERER(/* ClassName, */...)                                \
  /* This could be a typedef, except that some clients forward-declare it. */ \
  class INTERNAL_REGISTERER_CLASS_SUFFIX(Registerer, __VA_ARGS__)             \
      : public ::util_registration::ClassRegisterer<__VA_ARGS__> {            \
   public:                                                                    \
    using ClassRegisterer::ClassRegisterer;                                   \
  };                                                                          \
  using INTERNAL_REGISTERER_CLASS_SUFFIX(Creator, __VA_ARGS__) =              \
      INTERNAL_REGISTERER_CLASS_SUFFIX(Registerer,                            \
                                       __VA_ARGS__)::CreatorFunction

// The ellipsis below contains the factory return type
// (e.g. absl::StatusOr<ClassName*>) followed by the constructor argument
// types.
#define DEFINE_FACTORY_REGISTERER(ClassName, ...)                    \
  class ClassName##Registerer                                        \
      : public ::util_registration::FactoryRegisterer<__VA_ARGS__> { \
   public:                                                           \
    using CreatedType = ClassName;                                   \
    using FactoryRegisterer::FactoryRegisterer;                      \
  }

#define REGISTER_FACTORY_ENTITY(name, ClassName, Factory)       \
  static ::util_registration::internal::StaticRegistrationToken \
      name##_registration_token_ [[maybe_unused]] =             \
          ::util_registration::internal::RegisterStatically<    \
              ClassName##Registerer>(#name, __FILE__, Factory);

#define REGISTER_ENTITY(name, ClassName) \
  REGISTER_FACTORY_ENTITY(name, ClassName, ClassName##Registerer::Create<name>)

#define NEW_ENTITY_REGISTRATION_CB(name, ClassName, cb) \
  new ClassName##Registerer(name, __FILE__, cb)

#define REGISTER_SINGLETON_ENTITY(name, ClassName)              \
  REGISTER_FACTORY_ENTITY(name, ClassName, []() -> ClassName* { \
    static auto* instance = new name();                         \
    return instance;                                            \
  })

// Note: we use `ClassName##AliasRegisterer::ObjectType` instead of simply
// `ClassName`, because `ClassName` may not live in the namespace where this
// macro is invoked.
#define REGISTER_ALIAS(name, alias, ClassName)                    \
  static ::util_registration::internal::StaticRegistrationToken   \
      INTERNAL_REGISTER_CAT(name##_alias_registerer_,             \
                            __LINE__) [[maybe_unused]] =          \
          ::util_registration::internal::RegisterAliasStatically< \
              ClassName##AliasRegisterer::ObjectType>(#name, __FILE__, alias);

#endif  // THIRD_PARTY_GLOOP_UTIL_REGISTRATION_REGISTERER_H_
