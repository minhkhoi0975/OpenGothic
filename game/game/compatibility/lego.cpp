#include "lego.h"

#include "ikarus.h"
#include "game/gamescript.h"

#include <Tempest/Log>


using namespace Tempest;

LeGo::LeGo(GameScript& owner, Ikarus& ikarus, phoenix::vm& vm_) : owner(owner), ikarus(ikarus), vm(vm_) {
  Log::i("DMA mod detected: LeGo");

  // ## FrameFunctions
  vm.override_function("_FF_Create", [this](phoenix::func function, int delay, int cycles,
                                            int hasData, int data, bool gametime) {
    return _FF_Create(function, delay, cycles, hasData, data, gametime);
    });
  vm.override_function("FF_RemoveData", [this](phoenix::func function, int data){
    return FF_RemoveData(function, data);
    });
  vm.override_function("FF_ActiveData", [this](phoenix::func function, int data){
    return FF_ActiveData(function, data);
    });
  vm.override_function("FF_Active", [this](phoenix::func function){
    return FF_Active(function);
    });

  // HookEngine
  vm.override_function("HookEngineF", [](int address, int oldInstr, phoenix::func function) {
    auto sym  = function.value;
    auto name = sym==nullptr ? "" : sym->name().c_str();
    Log::e("not implemented call [HookEngineF] (", reinterpret_cast<void*>(uint64_t(address)),
           " -> ", name, ")");
    });
  vm.override_function("HookEngineI", [](int address, int oldInstr, phoenix::func function){
    auto sym  = function.value;
    auto name = sym==nullptr ? "" : sym->name().c_str();
    Log::e("not implemented call [HookEngineI] (", reinterpret_cast<void*>(uint64_t(address)),
           " -> ", name, ")");
    });

  // console commands
  vm.override_function("CC_Register", [](phoenix::func func, std::string_view prefix, std::string_view desc){
    auto sym  = func.value;
    auto name = sym==nullptr ? "" : sym->name().c_str();
    Log::e("not implemented call [CC_Register] (", prefix, " -> ", name, ")");
    });

  // various
  vm.override_function("_RENDER_INIT", [](){
    Log::e("not implemented call [_RENDER_INIT]");
    });
  vm.override_function("PRINT_FIXPS", [](){
    // function patches asm code of zCView::PrintTimed* to fix text coloring - we can ignore it
    });

  // ## PermMem
  vm.override_function("CREATE", [this](int inst) { return create(inst); });

  vm.override_function("LOCALS", [](){
    //NOTE: push local-variables to in-flight memory and restore at function end
    Log::e("TODO: LeGo-LOCALS.");
    });
  }

bool LeGo::isRequired(phoenix::vm& vm) {
  return
      vm.find_symbol_by_name("LeGo_InitFlags") != nullptr &&
      vm.find_symbol_by_name("LeGo_Init") != nullptr &&
         Ikarus::isRequired(vm);
  }

int LeGo::create(int instId) {
  auto *sym = vm.find_symbol_by_index(uint32_t(instId));
  auto *cls = sym;
  if(sym != nullptr && sym->type() == phoenix::datatype::instance) {
    cls = vm.find_symbol_by_index(sym->parent());
    }

  if(cls == nullptr) {
    Log::e("LeGo::create invalid symbold id (", instId, ")");
    return 0;
    }

  auto sz = cls->class_size();
  auto ptr = ikarus.mem_alloc(int32_t(sz));
  auto inst = std::make_shared<Ikarus::memory_instance>(ikarus, ptr);

  auto self = vm.find_symbol_by_name("SELF");
  auto prev = self != nullptr ? self->get_instance() : nullptr;
  if(self!=nullptr)
    self->set_instance(inst);
  vm.unsafe_call(sym);
  if(self!=nullptr)
    self->set_instance(prev);
  return ptr;
  }

void LeGo::tick(uint64_t dt) {
  auto time = owner.tickCount();

  auto ff = std::move(frameFunc);
  frameFunc.clear();

  for(auto& i:ff) {
    if(i.next>time) {
      frameFunc.push_back(i);
      continue;
      }

    if(auto* sym = vm.find_symbol_by_index(i.fncID)) {
      try {
      if(i.hasData)
        vm.call_function(sym, i.data); else
        vm.call_function(sym);
        }
      catch(const std::exception& e){
        Tempest::Log::e("exception in \"", sym->name(), "\": ",e.what());
        }
      if(i.cycles>0)
        i.cycles--;
      if(i.cycles==0)
        continue;
      i.next += uint64_t(i.delay);
      frameFunc.push_back(i);
      }
    }
  }

void LeGo::_FF_Create(phoenix::func func, int delay, int cycles, int hasData, int data, bool gametime) {
  FFItem itm;
  itm.fncID    = func.value->index();
  itm.cycles   = cycles;
  itm.delay    = std::max(delay, 0);
  itm.data     = data;
  itm.hasData  = hasData;
  itm.gametime = gametime;
  if(gametime) {
    itm.next = owner.tickCount() + uint64_t(delay);
    } else {
    itm.next = owner.tickCount(); // Timer() + itm.delay;
    };

  itm.cycles = std::max(itm.cycles, 0); // disable repetable callbacks for now
  frameFunc.emplace_back(itm);
  }

void LeGo::FF_Remove(phoenix::func function) {

  }

void LeGo::FF_RemoveAll(phoenix::func function) {

  }

void LeGo::FF_RemoveData(phoenix::func func, int data) {
  auto* sym = func.value;
  if(sym == nullptr) {
    Log::e("FF_RemoveData: invalid function ptr");
    return;
    }

  size_t nsz = 0;
  for(size_t i=0; i<frameFunc.size(); ++i) {
    if(frameFunc[i].fncID==sym->index() && frameFunc[i].data==data)
      continue;
    frameFunc[nsz] = frameFunc[i];
    ++nsz;
    }
  frameFunc.resize(nsz);
  }

bool LeGo::FF_ActiveData(phoenix::func func, int data) {
  auto* sym = func.value;
  if(sym == nullptr) {
    Log::e("FF_ActiveData: invalid function ptr");
    return false;
    }

  for(auto& f:frameFunc) {
    if(f.fncID==sym->index() && f.data==data)
      return true;
    }
  return false;
  }

bool LeGo::FF_Active(phoenix::func func) {
  auto* sym = func.value;
  if(sym == nullptr) {
    Log::e("FF_Active: invalid function ptr");
    return false;
    }

  for(auto& f:frameFunc) {
    if(f.fncID==sym->index())
      return true;
    }
  return false;
  }
