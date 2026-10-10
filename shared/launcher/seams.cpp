#include "seams.h"

#include <new>
#include <string>

#include "addresses_0_15_10.h"

namespace mcfm {
namespace launcher {
namespace {

// std::shared_ptr<T> by value is returned through x8; a type with a user-provided destructor
// gets the same treatment, so this function has the original's ABI.
struct SharedPtrOut {
  void *ptr;
  void *ctrl;
  ~SharedPtrOut() {}
};

// Seam #3: the Xbox Live services config singleton reads xboxservices.config through
// NSBundle (crashes without Foundation). Xbox Live is dropped, so hand out one zeroed
// object: MinecraftClient::init only stores strings into it (zeroed = empty std::string).
SharedPtrOut xbl_app_config() {
  alignas(16) static char config[0x400];
  SharedPtrOut out;
  out.ptr = config;
  out.ctrl = nullptr;
  return out;
}

// Seam #2: the iOS store wraps StoreKit's StoreManager (Objective-C, stubbed to nil) and
// crashes on its first query. createStores returns one store of our own instead: the Store
// interface (16 slots, order from the Android AndroidStore vtable) with no products, licensed,
// not a trial. Object layout as the iOS store: vptr, listener, two unused words.
namespace null_store {
void *destroy(void *self) { return self; }
void destroy_delete(void *self) { ::operator delete(self); }
bool no(void *) { return false; }
bool yes(void *) { return true; }
std::string store_id(void *) { return "mcfm-null"; }
std::string app_receipt(void *) { return std::string(); }
void ignore(void *) {}

void *const kVtable[16] = {
    reinterpret_cast<void *>(&destroy),         // ~Store()
    reinterpret_cast<void *>(&destroy_delete),  // ~Store() deleting
    reinterpret_cast<void *>(&no),              // requiresRestorePurchasesButton
    reinterpret_cast<void *>(&no),              // allowsSubscriptions
    reinterpret_cast<void *>(&store_id),        // getStoreId
    reinterpret_cast<void *>(&ignore),          // queryProducts(const std::vector<ProductId>&)
    reinterpret_cast<void *>(&ignore),          // purchase(const ProductId&, ProductType, const std::string&)
    reinterpret_cast<void *>(&ignore),          // acknowledgePurchase(const PurchaseInfo&, ProductType)
    reinterpret_cast<void *>(&ignore),          // queryPurchases
    reinterpret_cast<void *>(&ignore),          // restorePurchases
    reinterpret_cast<void *>(&no),              // isTrial
    reinterpret_cast<void *>(&ignore),          // purchaseGame
    reinterpret_cast<void *>(&yes),             // isGameLicensed
    reinterpret_cast<void *>(&app_receipt),     // getAppReceipt
    reinterpret_cast<void *>(&ignore),          // registerLicenseChangeCallback(std::function<void()>)
    reinterpret_cast<void *>(&ignore),          // handleLicenseChange
};
}  // namespace null_store

// std::vector<std::unique_ptr<Store>> by value: returned through x8 (user-provided destructor).
struct StoreVectorOut {
  void **begin, **end, **cap;
  ~StoreVectorOut() {}
};

StoreVectorOut create_stores(void * /*client*/, void *listener) {
  void **store = static_cast<void **>(::operator new(4 * sizeof(void *)));
  store[0] = const_cast<void **>(null_store::kVtable);
  store[1] = listener;
  store[2] = store[3] = nullptr;
  // StoreListener slot 2: onStoreInitialized(bool) — there is no store to use.
  reinterpret_cast<void (*)(void *, bool)>((*static_cast<void ***>(listener))[2])(listener, false);
  StoreVectorOut out;
  out.begin = static_cast<void **>(::operator new(sizeof(void *)));
  out.begin[0] = store;
  out.end = out.cap = out.begin + 1;
  return out;
}

// Seam #1: the engine's only use of the iOS HTTP glue (IOSHttpRequestGlue, NSURLConnection)
// is uploading telemetry event batches ("application/ms-maelstrom.v3+json;type=eventbatch")
// from the REST thread. Telemetry is dropped: the upload does nothing.
void telemetry_upload(void * /*self*/, void * /*body*/) {}

const Hook kHooks[] = {
    {"xbl_app_config", addr::kFnXblAppConfig, reinterpret_cast<void *>(&xbl_app_config)},
    {"create_stores", addr::kFnCreateStores, reinterpret_cast<void *>(&create_stores)},
    {"telemetry_upload", addr::kFnTelemetryUpload, reinterpret_cast<void *>(&telemetry_upload)},
};

}  // namespace

const Hook *hooks(size_t *count) {
  *count = sizeof kHooks / sizeof kHooks[0];
  return kHooks;
}

}  // namespace launcher
}  // namespace mcfm
