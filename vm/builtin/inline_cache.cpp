#include "detection.hpp"

#ifdef OS_X_10_5
#ifndef RBX_HAVE_TR1_HASH
#include "missing/leopard_hashtable.hpp"
#endif
#endif

#include "builtin/inline_cache.hpp"
#include "arguments.hpp"
#include "call_frame.hpp"
#include "global_cache.hpp"

#include "builtin/class.hpp"
#include "builtin/module.hpp"
#include "builtin/symbol.hpp"
#include "builtin/lookuptable.hpp"
#include "builtin/executable.hpp"
#include "builtin/methodtable.hpp"
#include "builtin/alias.hpp"
#include "builtin/call_unit.hpp"
#include "ontology.hpp"

namespace rubinius {

  void InlineCache::init(STATE) {
    GO(inline_cache).set(
        ontology::new_class(state, "InlineCache",
          G(call_site), G(rubinius)));
    G(inline_cache)->set_object_type(state, InlineCacheType);
  }

  InlineCache* InlineCache::empty(STATE, Symbol* name, Executable* executable, int ip) {
    InlineCache* cache =
      state->vm()->new_object_mature<InlineCache>(G(inline_cache));
    cache->name_ = name;
    cache->executor_ = CallSite::empty_cache;
    cache->fallback_ = CallSite::empty_cache;
    cache->updater_  = inline_cache_updater;
    cache->executable(state, executable);
    cache->ip_ = ip;
    cache->call_unit_ = nil<CallUnit>();
    cache->seen_classes_overflow_ = 0;
    cache->clear();
    return cache;
  }

  InlineCacheEntry* InlineCacheEntry::create(STATE, ClassData data, Class* klass, Module* mod,
                                             Executable* exec, MethodMissingReason method_missing) {
    InlineCacheEntry* cache =
      state->new_object_dirty<InlineCacheEntry>(G(object));

    cache->stored_module(state, mod);
    cache->receiver_class(state, klass);
    cache->method(state, exec);
    cache->receiver_ = data;
    cache->method_missing_ = method_missing;

    return cache;
  }

  Object* InlineCache::check_cache_custom(STATE, CallSite* call_site,
      CallFrame* call_frame, Arguments& args)
  {
    InlineCache* cache = static_cast<InlineCache*>(call_site);
    InlineCacheEntry* ice = cache->get_single_cache();

    return cache->call_unit_->execute(state, call_frame, cache->call_unit_,
                                      ice->method(), ice->stored_module(), args);
  }

  Object* InlineCache::check_cache_poly(STATE, CallSite* call_site, CallFrame* call_frame,
                                   Arguments& args)
  {
    Class* const recv_class = args.recv()->lookup_begin(state);

    InlineCacheEntry* entry;
    InlineCache* cache = static_cast<InlineCache*>(call_site);
    InlineCacheHit* ic = cache->get_inline_cache(recv_class, entry);

    if(likely(ic)) {
      if(entry->method_missing() != eNone) {
        args.unshift(state, call_site->name_);
        state->vm()->set_method_missing_reason(entry->method_missing());
      }
      Executable* meth = entry->method();
      Module* mod = entry->stored_module();
      ic->hit();

      return meth->execute(state, call_frame, meth, mod, args);
    }

    return cache->fallback(state, call_frame, args);
  }

  void InlineCache::inline_cache_updater(STATE, CallSite* call_site, Class* klass, FallbackExecutor fallback, Dispatch& dispatch) {
    InlineCache* cache = reinterpret_cast<InlineCache*>(call_site);
    InlineCacheEntry* entry = InlineCacheEntry::create(state, klass->data(), klass,
                                                       dispatch.module,
                                                       dispatch.method,
                                                       dispatch.method_missing);
    cache->write_barrier(state, entry);
    cache->set_cache(entry);
  }

  void InlineCache::print(STATE, std::ostream& stream) {
    stream << "name: " << name_->debug_str(state) << "\n"
           << "seen classes: " << classes_seen() << "\n"
           << "overflows: " << seen_classes_overflow_ << "\n"
           << "classes:\n";

    for(int i = 0; i < cTrackedICHits; i++) {
      if(cache_[i].entry()) {
        Module* mod = cache_[i].entry()->receiver_class();
        if(mod) {
          if(SingletonClass* sc = try_as<SingletonClass>(mod)) {
            if(Module* inner = try_as<Module>(sc->attached_instance())) {
              stream << "  SingletonClass:" << inner->debug_str(state);
            } else {
              stream << "  SingletonClass:" << sc->attached_instance()->class_object(state)->debug_str(state);
            }
          } else {
            stream << "  " << mod->debug_str(state);
          }

          stream << "\n";
        }
      }
    }
  }

  void InlineCache::Info::mark(Object* obj, ObjectMark& mark) {
    auto_mark(obj, mark);
    InlineCache* cache = static_cast<InlineCache*>(obj);

    for(int j = 0; j < cTrackedICHits; ++j) {
      InlineCacheEntry* ice = cache->cache_[j].entry();
      if(ice) {
        InlineCacheEntry* updated = static_cast<InlineCacheEntry*>(mark.call(ice));
        if(updated) {
          cache->cache_[j].update(updated);
          mark.just_set(cache, updated);
        }
      }
    }

    if(cache->call_unit_) {
      CallUnit* updated = static_cast<CallUnit*>(mark.call(cache->call_unit_));
      if(updated) {
        cache->call_unit_ = static_cast<CallUnit*>(updated);
        mark.just_set(cache, updated);
      }
    }
  }

}
